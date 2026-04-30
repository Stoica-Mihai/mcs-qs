#include "backend.hpp"

#include <qdbusconnection.h>
#include <qdbusmetatype.h>
#include <qlogging.h>
#include <qloggingcategory.h>

#include "../../core/logcat.hpp"
#include "screencast_session.hpp"

namespace qs::service::portal {

namespace {
QS_LOGGING_CATEGORY(logPortalBackend, "quickshell.service.portal", QtWarningMsg);
}

PortalBackend* PortalBackend::instance() {
	static auto* g = new PortalBackend();
	return g;
}

PortalBackend::PortalBackend(): QObject(nullptr) {
	// Register custom marshallers for portal reply types. ScreenCast's
	// `Start` returns `streams: a(ua{sv})` and QtDBus needs the
	// element type registered to encode it correctly.
	qDBusRegisterMetaType<ScreenCastStreamEntry>();
	qDBusRegisterMetaType<QList<ScreenCastStreamEntry>>();

	auto bus = QDBusConnection::sessionBus();
	if (!bus.isConnected()) {
		qCWarning(logPortalBackend) << "session bus not connected; portal disabled";
		return;
	}

	if (!bus.registerService("org.freedesktop.impl.portal.desktop.mcshell")) {
		qCWarning(logPortalBackend)
		    << "could not claim org.freedesktop.impl.portal.desktop.mcshell"
		    << "(already taken by another backend?)";
		return;
	}

	// Register the host with the default ExportAdaptors flag so any
	// QDBusAbstractAdaptor child added to *this* gets exposed
	// automatically. Adaptors are constructed by the per-interface QML
	// singletons (ScreenshotPortal, ScreenCastPortal, ...) and parented here.
	if (!bus.registerObject("/org/freedesktop/portal/desktop", this)) {
		qCWarning(logPortalBackend) << "could not register impl portal object";
		bus.unregisterService("org.freedesktop.impl.portal.desktop.mcshell");
		return;
	}

	this->mRegistered = true;
	qCInfo(logPortalBackend)
	    << "Portal backend registered at /org/freedesktop/portal/desktop";
}

} // namespace qs::service::portal
