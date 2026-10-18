# Quickshell.Services.Mpris on top of Quickshell.Windows.MediaSessions, see player.hpp.
qt_add_library(quickshell-windows-mpris STATIC
	player.cpp
	watcher.cpp
)

qt_add_qml_module(quickshell-windows-mpris
	URI Quickshell.Services.Mpris
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-windows-mpris quickshell-core)

install_qml_module(quickshell-windows-mpris)

target_link_libraries(quickshell-windows-mpris PRIVATE Qt::Quick quickshell-windows)

qs_module_pch(quickshell-windows-mpris)

target_link_libraries(quickshell PRIVATE quickshell-windows-mprisplugin)
