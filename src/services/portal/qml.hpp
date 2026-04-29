#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "screenshot.hpp"
#include "wallpaper.hpp"

namespace qs::service::portal {

///! xdg-desktop-portal Screenshot backend.
///
/// Registers as `org.freedesktop.impl.portal.desktop.mcshell` on the
/// session bus and serves `org.freedesktop.impl.portal.Screenshot` at
/// `/org/freedesktop/portal/desktop`. xdg-desktop-portal routes app
/// screenshot requests here when the active portal config selects
/// mcshell — see `mcshell.portal` (installed to
/// `/usr/share/xdg-desktop-portal/portals/`) for the matching declaration.
///
/// Listen for `requestReceived` and respond on the supplied
/// @@ScreenshotPortalRequest with respondWithFile(uri) / cancel() / fail()
/// to release the bus reply.
class ScreenshotPortal: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(ScreenshotPortal);
	QML_SINGLETON;

public:
	explicit ScreenshotPortal(QObject* parent = nullptr);

signals:
	/// Emitted whenever xdg-desktop-portal asks our backend to capture a
	/// screenshot.
	void requestReceived(qs::service::portal::ScreenshotPortalRequest* request);

private:
	ScreenshotImpl* impl = nullptr;
	friend class ScreenshotImpl;
};

///! xdg-desktop-portal Wallpaper backend.
///
/// Implements `org.freedesktop.impl.portal.Wallpaper` so apps can request
/// wallpaper changes via the portal API. Listen for `wallpaperRequested`
/// and call approve/cancel/fail on the supplied @@WallpaperPortalRequest
/// once the change is applied (or the user cancels the preview).
class WallpaperPortal: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(WallpaperPortal);
	QML_SINGLETON;

public:
	explicit WallpaperPortal(QObject* parent = nullptr);

signals:
	void wallpaperRequested(qs::service::portal::WallpaperPortalRequest* request);

private:
	WallpaperImpl* impl = nullptr;
	friend class WallpaperImpl;
};

} // namespace qs::service::portal
