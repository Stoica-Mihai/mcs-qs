qt_add_library(quickshell-service-portal STATIC
	backend.cpp
	qml.cpp
	screencast.cpp
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

target_link_libraries(quickshell-service-portal PRIVATE Qt::Qml Qt::DBus)
qs_add_link_dependencies(quickshell-service-portal quickshell-dbus)

target_link_libraries(quickshell PRIVATE quickshell-service-portalplugin)

qs_module_pch(quickshell-service-portal SET dbus)

# xdg-desktop-portal looks for *.portal config files in
# /usr/share/xdg-desktop-portal/portals/. Each declares which interfaces
# the named DBus service implements + which compositor it targets.
install(FILES mcshell.portal
	DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/xdg-desktop-portal/portals)
