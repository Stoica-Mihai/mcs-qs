#include "screencast.hpp"

#include <qdbusextratypes.h>
#include <qdbusmessage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
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
    const QVariantMap& /*options*/,
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
	// Picker UI lands in step 4. For now, accept the call so the caller's
	// sequence (CreateSession → SelectSources → Start) doesn't bail at
	// step two while we test the lifecycle.
	qCInfo(logScreenCast) << "SelectSources stub-accept:" << app_id
	                      << "session=" << session_handle.path();
	response = 0;
	results = {};
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
	qCInfo(logScreenCast) << "Start (skeleton):" << app_id
	                      << "session=" << session_handle.path();
	response = 2;
	results = {};
}

// ── ScreenCastPortal singleton ───────────────────────────────────

ScreenCastPortal::ScreenCastPortal(QObject* parent): QObject(parent) {
	this->impl = new ScreenCastImpl(PortalBackend::instance(), this);
}

} // namespace qs::service::portal
