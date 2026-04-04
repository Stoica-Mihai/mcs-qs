qt_add_library(quickshell-wayland-gamma-control STATIC
	proto.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-wayland-gamma-control
	URI Quickshell.Wayland._GammaControl
	VERSION 0.1
	DEPENDENCIES QtQml
)

install_qml_module(quickshell-wayland-gamma-control)

qs_add_module_deps_light(quickshell-wayland-gamma-control Quickshell)

wl_proto(wlp-gamma-control wlr-gamma-control-unstable-v1 "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(quickshell-wayland-gamma-control PRIVATE
	Qt::Quick Qt::WaylandClient Qt::WaylandClientPrivate wayland-client
	wlp-gamma-control
)

qs_module_pch(quickshell-wayland-gamma-control SET large)

target_link_libraries(quickshell PRIVATE quickshell-wayland-gamma-controlplugin)
