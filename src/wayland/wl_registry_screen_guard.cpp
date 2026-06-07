#include <dlfcn.h>
#include <unordered_map>

#include <private/qwaylanddisplay_p.h>
#include <private/qwaylandintegration_p.h>
#include <private/qwaylandscreen_p.h>
#include <private/qwaylandsurface_p.h>
#include <private/qwaylandwindow_p.h>
#include <qguiapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qscreen.h>
#include <qwindow.h>
#include <wayland-client-core.h>
#include <wayland-util.h>

#include "../core/logcat.hpp"

// Works around a Qt 6.11 use-after-free: when a wl_output global is removed,
// qtbase frees the QWaylandScreen, but QWaylandWindow::handleScreensChanged()
// reads QWaylandSurface::oldestEnteredScreen() (front of the surface's
// enter-screen list) before the dangling entry is purged, then derefs the
// freed screen in QPlatformScreen::screen() -> SIGSEGV.
//
// We interpose wl_registry's global_remove (via the same symbol-override
// mechanism as wl_proxy_safe_deref) so that, before qtbase frees the screen,
// we purge it from every surface's enter list — the same purge qtbase does
// itself, just early enough to beat the race.

namespace {
QS_LOGGING_CATEGORY(logScreenGuard, "quickshell.wayland.screenguard", QtWarningMsg);

using wl_proxy_add_listener_t = int (*)(wl_proxy*, void (**)(), void*);
wl_proxy_add_listener_t original_wl_proxy_add_listener = nullptr; // NOLINT

using GlobalRemoveFn = void (*)(void* data, void* registry, uint32_t name);

// Original global_remove handler keyed by the wl_registry proxy it belongs to.
std::unordered_map<void*, GlobalRemoveFn>& originalGlobalRemove() {
	static std::unordered_map<void*, GlobalRemoveFn> map;
	return map;
}

wl_proxy_add_listener_t resolveOriginalAddListener() {
	if (!original_wl_proxy_add_listener) {
		original_wl_proxy_add_listener =
		    reinterpret_cast<wl_proxy_add_listener_t>(dlsym(RTLD_NEXT, "wl_proxy_add_listener"));
	}
	return original_wl_proxy_add_listener;
}

// Drop the to-be-removed output's screen from every surface's enter list while
// the QWaylandScreen is still alive, so the later handleScreensChanged() can't
// deref freed memory.
void purgeDyingScreenFromSurfaces(uint32_t outputId) {
	auto* integration = QtWaylandClient::QWaylandIntegration::instance();
	if (!integration) return;

	auto* display = integration->display();
	if (!display) return;

	QtWaylandClient::QWaylandScreen* dying = nullptr;
	for (auto* screen: display->screens()) {
		if (screen && screen->outputId() == outputId) {
			dying = screen;
			break;
		}
	}

	if (!dying) return; // removed global was not an output

	auto* qscreen = dying->screen();
	if (!qscreen) return;

	const auto windows = QGuiApplication::allWindows();
	for (auto* window: windows) {
		auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
		if (!waylandWindow) continue;

		auto* surface = waylandWindow->waylandSurface();
		if (!surface) continue;

		QMetaObject::invokeMethod(
		    surface,
		    "handleScreenRemoved",
		    Qt::DirectConnection,
		    Q_ARG(QScreen*, qscreen)
		);
	}

	qCDebug(logScreenGuard) << "Purged dying output" << outputId << "from surfaces before screen free.";
}

void wrappedGlobalRemove(void* data, void* registry, uint32_t name) {
	purgeDyingScreenFromSurfaces(name);

	auto& map = originalGlobalRemove();
	auto it = map.find(registry);
	if (it != map.end() && it->second) it->second(data, registry, name);
}
} // namespace

extern "C" {
WL_EXPORT int wl_proxy_add_listener(wl_proxy* proxy, void (**implementation)(), void* data) {
	auto* original = resolveOriginalAddListener();

	using FnPtr = void (*)();
	const char* cls = proxy ? wl_proxy_get_class(proxy) : nullptr;
	if (cls && qstrcmp(cls, "wl_registry") == 0 && implementation) {
		// wl_registry_listener = { global, global_remove }
		auto* wrapped = new FnPtr[2]; // leaked once per registry (negligible)
		wrapped[0] = implementation[0];
		wrapped[1] = reinterpret_cast<FnPtr>(&wrappedGlobalRemove);

		originalGlobalRemove()[proxy] = reinterpret_cast<GlobalRemoveFn>(implementation[1]);

		qCInfo(logScreenGuard) << "Wrapped wl_registry global_remove for screen-removal guard.";
		return original(proxy, wrapped, data);
	}

	return original(proxy, implementation, data);
}
}

// NOLINTBEGIN (concurrency-mt-unsafe)
void installWlRegistryScreenGuard() {
	dlerror(); // clear old errors

	resolveOriginalAddListener();

	if (auto* error = dlerror()) {
		qCCritical(logScreenGuard) << "Failed to find wl_proxy_add_listener for hooking:" << error;
	} else {
		qCInfo(logScreenGuard) << "Installed wl_proxy_add_listener hook.";
	}
}
// NOLINTEND
