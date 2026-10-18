# Quickshell.Wayland so configs importing it load on Windows, see layershell.hpp.
# Only BackgroundEffect does anything, through Quickshell.Windows.BlurBehind.
qt_add_library(quickshell-windows-wayland STATIC
	layershell.cpp
	background_effect.cpp
	idle.hpp
)

qt_add_qml_module(quickshell-windows-wayland
	URI Quickshell.Wayland
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-windows-wayland quickshell-core quickshell-window)

install_qml_module(quickshell-windows-wayland)

target_link_libraries(quickshell-windows-wayland PRIVATE Qt::Quick quickshell-windows)

qs_module_pch(quickshell-windows-wayland)

target_link_libraries(quickshell PRIVATE quickshell-windows-waylandplugin)
