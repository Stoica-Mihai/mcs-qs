#include "screencast_session.hpp"

#include <qcoreapplication.h>
#include <qdbusargument.h>
#include <qdbusconnection.h>
#include <qdbusservicewatcher.h>
#include <qguiapplication.h>
#include <qhash.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qrect.h>
#include <qscreen.h>
#include <qstring.h>
#include <qtimer.h>
#include <qvariant.h>
#include <utility>

#include "../../core/logcat.hpp"
#include "screencast_stream.hpp"

namespace qs::service::portal {

namespace {
QS_LOGGING_CATEGORY(logScSession, "quickshell.service.portal.screencast", QtWarningMsg);
}

// ── ScreenCastSession ─────────────────────────────────────────────

QHash<QString, ScreenCastSession*>& ScreenCastSession::registry() {
	static QHash<QString, ScreenCastSession*> g;
	return g;
}

ScreenCastSession* ScreenCastSession::find(const QString& sessionHandlePath) {
	return registry().value(sessionHandlePath, nullptr);
}

ScreenCastSession::ScreenCastSession(
    QDBusObjectPath sessionHandle,
    QString appId,
    QString senderUniqueName,
    QObject* parent
)
    : QObject(parent)
    , mSessionHandle(std::move(sessionHandle))
    , mAppId(std::move(appId))
    , mSenderUniqueName(std::move(senderUniqueName)) {
	auto bus = QDBusConnection::sessionBus();

	this->mAdaptor = new ScreenCastSessionImpl(this, this);
	if (!bus.registerObject(this->mSessionHandle.path(), this)) {
		qCWarning(logScSession) << "could not register Session at" << this->mSessionHandle.path();
		// Caller-side response will be delivered as failure by the impl.
	}

	registry().insert(this->mSessionHandle.path(), this);

	// Watch the caller's unique name. If it disappears (crash / disconnect)
	// before Session.Close, tear down the session ourselves so we don't leak
	// resources or, later, a live PipeWire stream.
	if (!this->mSenderUniqueName.isEmpty()) {
		this->mWatcher = new QDBusServiceWatcher(
		    this->mSenderUniqueName,
		    bus,
		    QDBusServiceWatcher::WatchForUnregistration,
		    this
		);
		QObject::connect(
		    this->mWatcher,
		    &QDBusServiceWatcher::serviceUnregistered,
		    this,
		    &ScreenCastSession::onCallerLost
		);
	}

	qCInfo(logScSession) << "session opened:" << this->mSessionHandle.path()
	                     << "for" << this->mAppId
	                     << "(caller=" << this->mSenderUniqueName << ")";
}

ScreenCastSession::~ScreenCastSession() {
	this->close();
}

void ScreenCastSession::close() {
	if (this->mClosed) return;
	this->mClosed = true;
	emit this->closing(this);

	registry().remove(this->mSessionHandle.path());

	auto bus = QDBusConnection::sessionBus();
	bus.unregisterObject(this->mSessionHandle.path());

	qCInfo(logScSession) << "session closed:" << this->mSessionHandle.path();

	// Defer self-delete so any in-flight slot (e.g. Close) finishes returning
	// before we tear down the adaptor it lives on.
	this->deleteLater();
}

void ScreenCastSession::onCallerLost(const QString& service) {
	qCInfo(logScSession) << "caller dropped (" << service << "); closing session"
	                     << this->mSessionHandle.path();
	this->close();
}

bool ScreenCastSession::start(QDBusMessage startMessage) {
	if (this->mSelectedSourceIds.isEmpty()) {
		qCWarning(logScSession) << "Start called with no selected sources";
		return false;
	}

	auto paintCursors = (this->mCursorMode & 0x2u) != 0u; // Embedded
	auto screens = QGuiApplication::screens();

	for (const auto& id: this->mSelectedSourceIds) {
		// Source ids in step 6 are "output:<screen-name>". Toplevel ids
		// land alongside in step 8.
		if (!id.startsWith(QStringLiteral("output:"))) {
			qCWarning(logScSession) << "skipping unsupported source id" << id;
			continue;
		}
		auto name = id.mid(7);
		QScreen* screen = nullptr;
		for (auto* s: screens) {
			if (s->name() == name) { screen = s; break; }
		}
		if (screen == nullptr) {
			qCWarning(logScSession) << "selected screen no longer present:" << name;
			continue;
		}

		auto* stream = new ScreenCastStream(
		    ScreenCastStream::SourceKind::Output, screen, paintCursors, this
		);
		QObject::connect(stream, &ScreenCastStream::readyForCaller,
		                 this, &ScreenCastSession::onStreamReady);
		QObject::connect(stream, &ScreenCastStream::failed,
		                 this, &ScreenCastSession::onStreamFailed);
		this->mStreams.append(stream);
		stream->start();
	}

	if (this->mStreams.isEmpty()) {
		qCWarning(logScSession) << "Start: no usable streams could be built";
		return false;
	}

	this->mPendingStartReply = std::move(startMessage);
	this->mStartReplyPending = true;
	this->mStreamsReady = 0;
	return true;
}

void ScreenCastSession::onStreamReady() {
	this->mStreamsReady++;
	this->maybeFinishStart();
}

void ScreenCastSession::maybeFinishStart() {
	if (!this->mStartReplyPending) return;
	if (this->mStreamsReady < this->mStreams.size()) return;

	// Build the streams: a(ua{sv}) — one entry per stream.
	auto bus = QDBusConnection::sessionBus();
	auto reply = this->mPendingStartReply.createReply();
	this->mStartReplyPending = false;

	// Build streams as a proper a(ua{sv}) array. We register
	// QList<ScreenCastStreamEntry> with QtDBus (see header) so the
	// custom marshaller emits `a(ua{sv})`. QList<QVariant> encodes as
	// `av` instead and the xdg-desktop-portal frontend rejects the
	// reply silently, surfacing as NotAllowedError to the caller.
	QList<ScreenCastStreamEntry> entries;
	auto multi = this->mStreams.size() > 1;
	for (qsizetype i = 0; i < this->mStreams.size(); ++i) {
		auto* s = this->mStreams.at(i);
		ScreenCastStreamEntry e;
		e.nodeId = s->nodeId();
		e.props.insert(QStringLiteral("size"),
		               QVariant::fromValue(QPoint(s->width(), s->height())));
		e.props.insert(QStringLiteral("source_type"),
		               QVariant::fromValue(static_cast<quint32>(0x1u))); // Monitor
		// `mapping_id` correlates each stream back to a SelectSources
		// entry when multiple were requested. xdg-desktop-portal-frontend
		// uses this for stable per-stream identity (Chrome's display picker
		// labels each monitor share separately, etc.).
		if (multi) {
			e.props.insert(QStringLiteral("mapping_id"), QString::number(i));
		}
		entries.append(e);
	}

	QVariantMap results;
	results.insert(QStringLiteral("streams"), QVariant::fromValue(entries));
	// Persist mode echo. Spec: include `persist_mode` (0/1/2) and, when
	// non-zero, a `restore_token` the caller can store and pass back on
	// future SelectSources calls to skip the picker.
	if (this->mPersistMode > 0u && !this->mRestoreToken.isEmpty()) {
		results.insert(QStringLiteral("persist_mode"),
		               QVariant::fromValue(this->mPersistMode));
		results.insert(QStringLiteral("restore_token"), this->mRestoreToken);
	}
	reply.setArguments({QVariant(static_cast<quint32>(0u)), QVariant::fromValue(results)});

	bus.send(reply);
	qCInfo(logScSession) << "Start reply sent: " << this->mStreams.size() << "stream(s)";
}

void ScreenCastSession::onStreamFailed(const QString& reason) {
	qCWarning(logScSession) << "stream failed:" << reason;
	this->cancelStartReply(2);
}

void ScreenCastSession::cancelStartReply(quint32 response) {
	if (!this->mStartReplyPending) return;
	this->mStartReplyPending = false;
	auto bus = QDBusConnection::sessionBus();
	auto reply = this->mPendingStartReply.createReply();
	reply.setArguments({QVariant(response), QVariant::fromValue(QVariantMap{})});
	bus.send(reply);
}

// ── ScreenCastSessionImpl ─────────────────────────────────────────

ScreenCastSessionImpl::ScreenCastSessionImpl(QObject* parent, ScreenCastSession* session)
    : QDBusAbstractAdaptor(parent), session(session) {}

void ScreenCastSessionImpl::Close() {
	if (this->session == nullptr) return;
	auto* s = this->session;
	this->session = nullptr;
	s->close();
}

} // namespace qs::service::portal
