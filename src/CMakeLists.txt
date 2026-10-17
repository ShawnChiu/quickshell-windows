qt_add_executable(quickshell main.cpp)

install(TARGETS quickshell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if (WIN32)
	# symlinks need extra privileges on windows, so qs is a copy instead
	add_custom_command(TARGET quickshell POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:quickshell> $<TARGET_FILE_DIR:quickshell>/qs.exe
	)
endif()

add_subdirectory(build)
add_subdirectory(launch)
add_subdirectory(core)
add_subdirectory(debug)
add_subdirectory(ipc)
add_subdirectory(window)
add_subdirectory(io)
add_subdirectory(widgets)
add_subdirectory(ui)
add_subdirectory(windowmanager)

if (CRASH_HANDLER)
	add_subdirectory(crash)
endif()

if (DBUS)
	add_subdirectory(dbus)
endif()

if (WAYLAND)
	add_subdirectory(wayland)
endif()

if (X11)
	add_subdirectory(x11)
endif()

if (I3)
	add_subdirectory(x11/i3)
endif()

if (WINDOWS_SHELL)
	add_subdirectory(windows)
endif()

add_subdirectory(services)

if (BLUETOOTH)
	add_subdirectory(bluetooth)
endif()

if (NETWORK)
	add_subdirectory(network)
endif()
