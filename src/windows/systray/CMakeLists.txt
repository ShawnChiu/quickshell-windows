# Quickshell.Services.SystemTray on top of Quickshell.Windows.NotifyIcons, see item.hpp.
qt_add_library(quickshell-windows-systray STATIC
	item.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-windows-systray
	URI Quickshell.Services.SystemTray
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-windows-systray quickshell-core quickshell-window)

install_qml_module(quickshell-windows-systray)

target_link_libraries(quickshell-windows-systray PRIVATE Qt::Quick quickshell-windows)

qs_module_pch(quickshell-windows-systray)

target_link_libraries(quickshell PRIVATE quickshell-windows-systrayplugin)
