#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "screencast.hpp"
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

///! xdg-desktop-portal ScreenCast backend.
///
/// Implements `org.freedesktop.impl.portal.ScreenCast` so apps using
/// `xdg-desktop-portal` for screen-share (browsers, OBS, Zoom, etc.)
/// route through mcshell instead of `xdg-desktop-portal-wlr`. Skeleton
/// stage: the bus surface is registered (introspectable, version=4)
/// but every method currently fails with response=2. Picker UI and
/// real PipeWire stream wiring land in follow-up steps; see
/// PLAN-screencast-portal.md.
class ScreenCastPortal: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(ScreenCastPortal);
	QML_SINGLETON;

public:
	explicit ScreenCastPortal(QObject* parent = nullptr);

signals:
	/// Emitted when an app calls `SelectSources` and we need the user to
	/// pick what to share. Fill in `selectedSourceIds` on the request,
	/// then call approve() (or cancel/fail) to release the bus reply.
	void pickerRequested(qs::service::portal::ScreenCastPickerRequest* request);

private:
	ScreenCastImpl* impl = nullptr;
	friend class ScreenCastImpl;
};

} // namespace qs::service::portal
