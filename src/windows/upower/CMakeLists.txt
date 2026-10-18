# Quickshell.Services.UPower on top of Quickshell.Windows.PowerManager, see core.hpp.
qt_add_library(quickshell-windows-upower STATIC
	core.cpp
	powerprofiles.cpp
)

qt_add_qml_module(quickshell-windows-upower
	URI Quickshell.Services.UPower
	VERSION 0.1
	DEPENDENCIES QtQml
)

qs_add_module_deps_light(quickshell-windows-upower quickshell-core)

install_qml_module(quickshell-windows-upower)

target_link_libraries(quickshell-windows-upower PRIVATE Qt::Qml quickshell-windows)

qs_module_pch(quickshell-windows-upower)

target_link_libraries(quickshell PRIVATE quickshell-windows-upowerplugin)
