#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "screencast.hpp"
#include "screenshot.hpp"

class QScreen;
namespace qs::wayland::screencopy { class ScreencopyContext; }

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

	/// Active instance (the QML singleton). null until QML has loaded
	/// the type at least once. C++ users (the impl-portal session glue)
	/// reach for this to share state across sessions.
	[[nodiscard]] static ScreenCastPortal* instance() { return sInstance; }

	/// Get-or-create a long-lived screencopy context for `screen` with
	/// the given cursor mode. Cached on the portal so successive
	/// ScreenCast sessions reusing the same monitor share one underlying
	/// Wayland capture session — avoids racing the wl_buffer teardown
	/// when a session ends and a new one starts up shortly after for
	/// the same screen.
	[[nodiscard]] qs::wayland::screencopy::ScreencopyContext*
	getOrCreateScreencopy(QScreen* screen, bool paintCursors);

signals:
	/// Emitted when an app calls `SelectSources` and we need the user to
	/// pick what to share. Fill in `selectedSourceIds` on the request,
	/// then call approve() (or cancel/fail) to release the bus reply.
	void pickerRequested(qs::service::portal::ScreenCastPickerRequest* request);

private:
	struct CtxKey {
		QScreen* screen;
		bool paintCursors;
		bool operator==(const CtxKey& o) const noexcept {
			return this->screen == o.screen && this->paintCursors == o.paintCursors;
		}
	};
	struct CtxKeyHash {
		std::size_t operator()(const CtxKey& k) const noexcept {
			return std::hash<QScreen*>{}(k.screen) ^ std::hash<bool>{}(k.paintCursors);
		}
	};

	ScreenCastImpl* impl = nullptr;
	std::unordered_map<CtxKey, qs::wayland::screencopy::ScreencopyContext*, CtxKeyHash>
	    mScreencopyCache;
	static ScreenCastPortal* sInstance;
	friend class ScreenCastImpl;
};

} // namespace qs::service::portal
