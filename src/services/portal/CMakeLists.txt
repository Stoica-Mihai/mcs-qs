qt_add_library(quickshell-service-portal STATIC
	backend.cpp
	qml.cpp
	screencast.cpp
	screencast_persist.cpp
	screencast_session.cpp
	screencast_stream.cpp
	screenshot.cpp
	wallpaper.cpp
)

qt_add_qml_module(quickshell-service-portal
	URI Quickshell.Services.Portal
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-portal Quickshell)

install_qml_module(quickshell-service-portal)

find_package(PkgConfig REQUIRED)
pkg_check_modules(pipewire REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(libdrm REQUIRED IMPORTED_TARGET libdrm)

target_link_libraries(quickshell-service-portal PRIVATE
	Qt::Qml Qt::DBus Qt::Gui Qt::Quick
	Qt::WaylandClientPrivate
	PkgConfig::pipewire
	PkgConfig::libdrm
	wlp-linux-dmabuf
)
qs_add_link_dependencies(quickshell-service-portal
	quickshell-dbus
	quickshell-wayland-buffer
	quickshell-wayland-screencopy
	quickshell-service-pipewire
)

target_link_libraries(quickshell PRIVATE quickshell-service-portalplugin)

qs_module_pch(quickshell-service-portal SET dbus)

# xdg-desktop-portal scans a fixed set of paths for *.portal config
# files (typically /usr/share/xdg-desktop-portal/portals + the user's
# XDG_DATA_DIRS prefixes). When CMAKE_INSTALL_PREFIX is /usr/local
# (the default), the standard ${CMAKE_INSTALL_DATAROOTDIR} target
# leaves the file outside that scan path and the frontend can't see
# our backend. Anchor the install at the system datadir resolved from
# pkg-config so it lands wherever this distro keeps gtk.portal.
pkg_get_variable(XDP_PORTAL_DIR xdg-desktop-portal portaldir)
if(NOT XDP_PORTAL_DIR)
	set(XDP_PORTAL_DIR /usr/share/xdg-desktop-portal/portals)
endif()
install(FILES mcshell.portal DESTINATION ${XDP_PORTAL_DIR})
