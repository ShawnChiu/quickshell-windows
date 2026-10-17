#include "hwnd.hpp"

#include <qtypes.h>

#include <windows.h>

namespace qs::windows {

namespace {

HWND toHwnd(qint64 hwnd) { return reinterpret_cast<HWND>(static_cast<quintptr>(hwnd)); } // NOLINT

} // namespace

bool HwndActions::activate(qint64 hwnd) {
	auto* window = toHwnd(hwnd);
	if (!IsWindow(window)) return false;

	if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
	if (SetForegroundWindow(window)) return true;

	// Only the process with the last input event may take the foreground. An empty
	// input event makes this process that one.
	INPUT input {};
	input.type = INPUT_MOUSE;
	SendInput(1, &input, sizeof(input));
	return SetForegroundWindow(window) != 0;
}

bool HwndActions::close(qint64 hwnd) {
	auto* window = toHwnd(hwnd);
	return IsWindow(window) && PostMessageW(window, WM_CLOSE, 0, 0) != 0;
}

qint32 HwndActions::processId(qint64 hwnd) {
	DWORD pid = 0;
	auto* window = toHwnd(hwnd);
	if (IsWindow(window)) GetWindowThreadProcessId(window, &pid);
	return static_cast<qint32>(pid);
}

} // namespace qs::windows
