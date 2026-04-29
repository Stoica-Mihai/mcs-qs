#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>

#include "screenshot.hpp"

namespace qs::service::portal {

///! xdg-desktop-portal Screenshot backend.
///
/// Registers `org.freedesktop.impl.portal.desktop.mcshell` on the session
/// bus and serves the `org.freedesktop.impl.portal.Screenshot` interface.
/// When apps invoke the user-facing `org.freedesktop.portal.Screenshot`,
/// xdg-desktop-portal routes the call to whichever backend the active
/// portal config selects — see `mcshell.portal` (installed to
/// `/usr/share/xdg-desktop-portal/portals/`) for the matching declaration.
///
/// Listen for `requestReceived` and respond on the supplied
/// @@ScreenshotPortalRequest. If the request is interactive, render a UI;
/// otherwise capture immediately. Either way, call `respondWithFile(uri)`
/// (success), `cancel()` (user cancelled), or `fail()` (capture failed)
/// to release the bus reply.
class ScreenshotPortal: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(ScreenshotPortal);
	QML_SINGLETON;

	Q_PROPERTY(bool registered READ registered NOTIFY registeredChanged);

public:
	explicit ScreenshotPortal(QObject* parent = nullptr);

	[[nodiscard]] bool registered() const { return this->mRegistered; }

signals:
	void registeredChanged();
	/// Emitted whenever xdg-desktop-portal asks our backend to capture a
	/// screenshot. Answer the request via one of its respond/cancel/fail
	/// invokables to release the bus reply.
	void requestReceived(qs::service::portal::ScreenshotPortalRequest* request);

private:
	void tryRegister();

	bool mRegistered = false;
	ScreenshotImpl* impl = nullptr;
	friend class ScreenshotImpl;
};

} // namespace qs::service::portal
