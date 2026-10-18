qt_add_library(quickshell-windows STATIC
	panel_window.cpp
	idle.cpp
	hwnd.cpp
	blur_behind.cpp
	backdrop.cpp
)

qt_add_qml_module(quickshell-windows
	URI Quickshell.Windows
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-windows quickshell-core quickshell-window)

install_qml_module(quickshell-windows)

add_library(quickshell-windows-init OBJECT init.cpp)

target_link_libraries(quickshell-windows PRIVATE Qt::Quick shell32 user32 gdi32 advapi32 dwmapi dxguid windowsapp CoreMessaging)
target_link_libraries(quickshell-windows-init PRIVATE Qt::Quick Qt::Qml)

qs_module_pch(quickshell-windows SET large)

target_link_libraries(quickshell PRIVATE quickshell-windowsplugin quickshell-windows-init)

# Quickshell.Wayland is only provided as a stand-in when the real module is not built.
if (NOT WAYLAND)
	add_subdirectory(wayland)
endif()
