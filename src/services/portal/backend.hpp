#pragma once

#include <qobject.h>

namespace qs::service::portal {

/// Single host for the impl-portal service registration. Each portal
/// interface implementation lives as a QDBusAbstractAdaptor attached to
/// this object; QtDBus auto-discovers them when registerObject is called
/// with the default ExportAdaptors flag, so all interfaces appear at
/// `/org/freedesktop/portal/desktop` under
/// `org.freedesktop.impl.portal.desktop.mcshell`.
class PortalBackend: public QObject {
	Q_OBJECT;

public:
	[[nodiscard]] static PortalBackend* instance();
	[[nodiscard]] bool isRegistered() const { return this->mRegistered; }

private:
	explicit PortalBackend();
	bool mRegistered = false;
};

} // namespace qs::service::portal
