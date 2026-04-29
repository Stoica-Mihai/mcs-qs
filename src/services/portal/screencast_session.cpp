#include "screencast_session.hpp"

#include <qdbusconnection.h>
#include <qdbusservicewatcher.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtimer.h>
#include <utility>

#include "../../core/logcat.hpp"

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
