#include "monitor.hpp"

#include <qscreen.h>
#include <qscreen_platform.h>
#include <qstring.h>

#include <windows.h>

#include "../core/qmlscreen.hpp"

namespace qs::windows {

QString MonitorInfo::deviceName(QuickshellScreenInfo* screen) {
	if (!screen || !screen->screen) return {};

	auto* native = screen->screen->nativeInterface<QNativeInterface::QWindowsScreen>();
	if (!native) return {};

	auto info = MONITORINFOEXW {};
	info.cbSize = sizeof(MONITORINFOEXW);
	if (!GetMonitorInfoW(native->handle(), &info)) return {};

	return QString::fromWCharArray(info.szDevice);
}

} // namespace qs::windows
