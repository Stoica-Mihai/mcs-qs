#include "screencast.hpp"

#include <utility>

#include <qdbusconnection.h>
#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qguiapplication.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qpointer.h>
#include <qscreen.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"
#include "backend.hpp"
#include "qml.hpp"
#include "screencast_session.hpp"

namespace qs::service::portal {

namespace {
QS_LOGGING_CATEGORY(logScreenCast, "quickshell.service.portal.screencast", QtWarningMsg);
}

ScreenCastImpl::ScreenCastImpl(QObject* parent, ScreenCastPortal* portal)
    : QDBusAbstractAdaptor(parent), portal(portal) {}

// ── ScreenCastPickerSource ────────────────────────────────────────

ScreenCastPickerSource::ScreenCastPickerSource(
    QObject* parent,
    QString id,
    QString label,
    quint32 sourceType,
    int width,
    int height
)
    : QObject(parent)
    , mId(std::move(id))
    , mLabel(std::move(label))
    , mSourceType(sourceType)
    , mWidth(width)
    , mHeight(height) {}

// ── ScreenCastPickerRequest ───────────────────────────────────────

ScreenCastPickerRequest::ScreenCastPickerRequest(
    QObject* parent,
    QString appId,
    QString sessionHandle,
    quint32 sourceTypes,
    bool multiple,
    quint32 cursorMode,
    quint32 persistMode,
    QString restoreToken,
    QList<ScreenCastPickerSource*> availableSources,
    QDBusMessage message
)
    : QObject(parent)
    , mAppId(std::move(appId))
    , mSessionHandle(std::move(sessionHandle))
    , mSourceTypes(sourceTypes)
    , mMultiple(multiple)
    , mCursorMode(cursorMode)
    , mPersistMode(persistMode)
    , mRestoreToken(std::move(restoreToken))
    , mAvailableSources(std::move(availableSources))
    , mMessage(std::move(message)) {
	for (auto* src: this->mAvailableSources) {
		src->setParent(this);
	}
}

void ScreenCastPickerRequest::setSelectedSourceIds(const QStringList& ids) {
	this->mSelectedIds = ids;
}

void ScreenCastPickerRequest::sendResponse(quint32 response) {
	if (this->mAnswered) return;
	this->mAnswered = true;

	auto reply = this->mMessage.createReply({QVariant(response), QVariant::fromValue(QVariantMap {})});
	QDBusConnection::sessionBus().send(reply);

	// Stamp the selection on the owning session so Start() can find it.
	if (response == 0) {
		auto* session = ScreenCastSession::find(this->mSessionHandle);
		if (session != nullptr) {
			session->setSelectedSourceIds(this->mSelectedIds);
		}
	}

	emit this->answeredChanged();
	this->deleteLater();
}

void ScreenCastPickerRequest::approve() { this->sendResponse(0); }
void ScreenCastPickerRequest::cancel() { this->sendResponse(1); }
void ScreenCastPickerRequest::fail() { this->sendResponse(2); }

void ScreenCastImpl::CreateSession(
    const QDBusObjectPath& /*handle*/,
    const QDBusObjectPath& session_handle,
    const QString& app_id,
    const QVariantMap& /*options*/,
    quint32& response,
    QVariantMap& results
) {
	auto* backend = PortalBackend::instance();
	auto sender = backend->message().service(); // unique caller name, e.g. ":1.42"

	if (ScreenCastSession::find(session_handle.path()) != nullptr) {
		qCWarning(logScreenCast)
		    << "CreateSession: handle already in use:" << session_handle.path();
		response = 2;
		results = {};
		return;
	}

	new ScreenCastSession(session_handle, app_id, sender, this->portal);
	qCInfo(logScreenCast) << "CreateSession ok:" << app_id
	                      << "session=" << session_handle.path();
	response = 0;
	results = {};
}

void ScreenCastImpl::SelectSources(
    const QDBusObjectPath& /*handle*/,
    const QDBusObjectPath& session_handle,
    const QString& app_id,
    const QVariantMap& options,
    quint32& response,
    QVariantMap& results
) {
	auto* session = ScreenCastSession::find(session_handle.path());
	if (session == nullptr) {
		qCWarning(logScreenCast)
		    << "SelectSources: unknown session" << session_handle.path();
		response = 2;
		results = {};
		return;
	}

	auto sourceTypes =
	    options.value("types", ScreenCastSourceType::Monitor).value<quint32>();
	auto multiple = options.value("multiple", false).toBool();
	auto cursorMode =
	    options.value("cursor_mode", ScreenCastCursorMode::Hidden).value<quint32>();
	auto persistMode = options.value("persist_mode", 0u).value<quint32>();
	auto restoreToken = options.value("restore_token").toString();

	session->setCursorMode(cursorMode);
	session->setPersistMode(persistMode);

	// Build the available-sources list. Outputs always go in; window
	// (toplevel) sources land alongside in step 5 once the picker UI
	// can render them. Virtual is unsupported.
	auto sources = QList<ScreenCastPickerSource*>();
	if ((sourceTypes & ScreenCastSourceType::Monitor) != 0) {
		auto screens = QGuiApplication::screens();
		for (qsizetype i = 0; i < screens.size(); ++i) {
			auto* s = screens.at(i);
			auto id = QStringLiteral("output:") + s->name();
			auto label = s->name();
			if (!s->manufacturer().isEmpty() || !s->model().isEmpty()) {
				label = s->manufacturer() + ' ' + s->model();
			}
			sources.append(new ScreenCastPickerSource(
			    nullptr,
			    id,
			    label.trimmed().isEmpty() ? s->name() : label.trimmed(),
			    ScreenCastSourceType::Monitor,
			    s->geometry().width(),
			    s->geometry().height()
			));
		}
	}

	auto* backend = PortalBackend::instance();
	backend->setDelayedReply(true);
	auto* req = new ScreenCastPickerRequest(
	    this->portal,
	    app_id,
	    session_handle.path(),
	    sourceTypes,
	    multiple,
	    cursorMode,
	    persistMode,
	    restoreToken,
	    sources,
	    backend->message()
	);

	qCInfo(logScreenCast) << "SelectSources picker:" << app_id
	                      << "types=" << sourceTypes
	                      << "multiple=" << multiple
	                      << "sources=" << sources.size();
	emit this->portal->pickerRequested(req);
}

void ScreenCastImpl::Start(
    const QDBusObjectPath& /*handle*/,
    const QDBusObjectPath& session_handle,
    const QString& app_id,
    const QString& /*parent_window*/,
    const QVariantMap& /*options*/,
    quint32& response,
    QVariantMap& results
) {
	auto* session = ScreenCastSession::find(session_handle.path());
	if (session == nullptr) {
		qCWarning(logScreenCast) << "Start: unknown session" << session_handle.path();
		response = 2;
		results = {};
		return;
	}
	if (session->selectedSourceIds().isEmpty()) {
		qCWarning(logScreenCast)
		    << "Start: SelectSources was not invoked or yielded nothing";
		response = 2;
		results = {};
		return;
	}

	qCInfo(logScreenCast) << "Start:" << app_id
	                      << "session=" << session_handle.path()
	                      << "sources=" << session->selectedSourceIds().size();

	auto* backend = PortalBackend::instance();
	backend->setDelayedReply(true);
	if (!session->start(backend->message())) {
		// session.start() failed synchronously — release the delayed
		// reply with an error response by re-using the captured message.
		auto reply = backend->message().createReply();
		reply.setArguments({QVariant(static_cast<quint32>(2u)),
		                    QVariant::fromValue(QVariantMap{})});
		QDBusConnection::sessionBus().send(reply);
	}
}

// ── ScreenCastPortal singleton ───────────────────────────────────

ScreenCastPortal::ScreenCastPortal(QObject* parent): QObject(parent) {
	this->impl = new ScreenCastImpl(PortalBackend::instance(), this);
}

} // namespace qs::service::portal
