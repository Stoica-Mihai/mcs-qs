qt_add_library(quickshell-wayland-data-control STATIC
	proto.cpp
	history.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-wayland-data-control
	URI Quickshell.Wayland._DataControl
	VERSION 0.1
	DEPENDENCIES QtQml
)

install_qml_module(quickshell-wayland-data-control)

qs_add_module_deps_light(quickshell-wayland-data-control Quickshell)

wl_proto(wlp-data-control ext-data-control-v1 "${WAYLAND_PROTOCOLS}/staging/ext-data-control")

target_link_libraries(quickshell-wayland-data-control PRIVATE
	Qt::Quick Qt::WaylandClient Qt::WaylandClientPrivate wayland-client
	wlp-data-control
)

qs_module_pch(quickshell-wayland-data-control SET large)

target_link_libraries(quickshell PRIVATE quickshell-wayland-data-controlplugin)
