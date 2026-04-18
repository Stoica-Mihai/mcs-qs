qt_add_library(quickshell-service-sysinfo STATIC
	sysinfo.cpp
	gpu.cpp
)

qt_add_qml_module(quickshell-service-sysinfo
	URI Quickshell.Services.SysInfo
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-service-sysinfo Quickshell)

install_qml_module(quickshell-service-sysinfo)

target_link_libraries(quickshell-service-sysinfo PRIVATE Qt::Qml ${CMAKE_DL_LIBS})
target_link_libraries(quickshell PRIVATE quickshell-service-sysinfoplugin)

qs_module_pch(quickshell-service-sysinfo)
