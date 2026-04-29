set_source_files_properties(org.freedesktop.login1.Manager.xml PROPERTIES
	CLASSNAME DBusLogindManager
	NO_NAMESPACE TRUE
)

qt_add_dbus_interface(DBUS_INTERFACES
	org.freedesktop.login1.Manager.xml
	dbus_manager
)

set_source_files_properties(org.freedesktop.login1.Session.xml PROPERTIES
	CLASSNAME DBusLogindSession
	NO_NAMESPACE TRUE
)

qt_add_dbus_interface(DBUS_INTERFACES
	org.freedesktop.login1.Session.xml
	dbus_session
)

qt_add_library(quickshell-service-logind STATIC
	qml.cpp
	${DBUS_INTERFACES}
)

target_include_directories(quickshell-service-logind PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

qt_add_qml_module(quickshell-service-logind
	URI Quickshell.Services.Logind
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-logind Quickshell)

install_qml_module(quickshell-service-logind)

target_link_libraries(quickshell-service-logind PRIVATE Qt::Qml Qt::DBus)
qs_add_link_dependencies(quickshell-service-logind quickshell-dbus)

target_link_libraries(quickshell PRIVATE quickshell-service-logindplugin)

qs_module_pch(quickshell-service-logind SET dbus)
