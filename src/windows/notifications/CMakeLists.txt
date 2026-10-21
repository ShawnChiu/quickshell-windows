# Quickshell.Services.Notifications on top of Quickshell.Windows.Toasts, see notification.hpp.
qt_add_library(quickshell-windows-notifications STATIC
	server.cpp
	notification.cpp
	qml.cpp
)

qt_add_qml_module(quickshell-windows-notifications
	URI Quickshell.Services.Notifications
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-windows-notifications Quickshell)

install_qml_module(quickshell-windows-notifications)

target_link_libraries(quickshell-windows-notifications PRIVATE Qt::Quick quickshell-windows)

qs_module_pch(quickshell-windows-notifications)

target_link_libraries(quickshell PRIVATE quickshell-windows-notificationsplugin)
