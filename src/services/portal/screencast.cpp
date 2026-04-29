#include "screencast.hpp"

#include <qdbusextratypes.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/logcat.hpp"
#include "backend.hpp"
#include "qml.hpp"

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
	qCInfo(logScreenCast) << "CreateSession (skeleton):" << app_id
	                      << "session=" << session_handle.path();
	response = 2;
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
	qCInfo(logScreenCast) << "SelectSources (skeleton):" << app_id
	                      << "session=" << session_handle.path();
	response = 2;
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
