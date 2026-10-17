if (WIN32)
	# CLI11 is rarely packaged on windows. It is header only, so fetch it when it isn't installed.
	include(FetchContent)
	FetchContent_Declare(CLI11
		GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
		GIT_TAG v2.4.2
		GIT_SHALLOW TRUE
		FIND_PACKAGE_ARGS CONFIG
	)
	FetchContent_MakeAvailable(CLI11)
endif()

find_package(CLI11 CONFIG REQUIRED)

qt_add_library(quickshell-launch STATIC
	parsecommand.cpp
	command.cpp
	launch.cpp
	main.cpp
)

target_link_libraries(quickshell-launch PRIVATE
	Qt::Quick Qt::Widgets CLI11::CLI11 quickshell-build
)

qs_add_pchset(launch
	DEPENDENCIES Qt::Core CLI11::CLI11
	HEADERS
		<CLI/App.hpp>
		<qcoreapplication.h>
)

qs_pch(quickshell-launch SET launch)

target_link_libraries(quickshell PRIVATE quickshell-launch)
