qt_add_library(quickshell-dbus-server STATIC
	handler.cpp
)

qt_add_qml_module(quickshell-dbus-server
	URI Quickshell.DBus
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-dbus-server Quickshell)

install_qml_module(quickshell-dbus-server)

target_link_libraries(quickshell-dbus-server PRIVATE Qt::Qml Qt::DBus)
qs_add_link_dependencies(quickshell-dbus-server quickshell-dbus)

qs_module_pch(quickshell-dbus-server SET dbus)

target_link_libraries(quickshell PRIVATE quickshell-dbus-serverplugin)
