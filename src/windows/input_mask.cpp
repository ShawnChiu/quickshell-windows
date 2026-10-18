#include "input_mask.hpp"

// clang-format off
#include <windows.h>
#include <windowsx.h>
// clang-format on

#include <qabstractnativeeventfilter.h>
#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qlist.h>
#include <qnamespace.h>
#include <qpoint.h>
#include <qpointer.h>
#include <qregion.h>
#include <qtimer.h>
#include <qtypes.h>
#include <qwindow.h>

namespace qs::windows {

namespace {

struct MaskedWindow {
	QPointer<QWindow> window;
	// Logical pixels, relative to the window.
	QRegion mask;
	// Transparent to input, because the pointer is outside the mask.
	bool passing = false;
};

QList<MaskedWindow>& masked() {
	static QList<MaskedWindow> masked; // NOLINT
	return masked;
}

HWND hwndOf(const QWindow* window) {
	return window && window->handle() ? reinterpret_cast<HWND>(window->winId()) : nullptr; // NOLINT
}

// Whether the screen point, in device pixels, is over the window but outside its mask.
bool outsideMask(const MaskedWindow& entry, POINT point) {
	auto* hwnd = hwndOf(entry.window);
	if (!hwnd || !IsWindowVisible(hwnd)) return false;

	RECT rect;
	GetWindowRect(hwnd, &rect);
	if (!PtInRect(&rect, point)) return false;

	auto scale = entry.window->devicePixelRatio();
	auto local = QPointF(point.x - rect.left, point.y - rect.top) / scale;
	return !entry.mask.contains(local.toPoint());
}

void setPassing(MaskedWindow& entry, bool passing);

// Only layered windows can be transparent to input. Kept while masked, since toggling it changes
// how the window is composed.
void ensureLayered(HWND hwnd) {
	auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	if (style & WS_EX_LAYERED) return;

	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
	SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

QTimer& pointerPoll() {
	static auto* timer = [] {
		auto* timer = new QTimer(); // NOLINT
		// Fast enough that the body takes the pointer back before a click.
		timer->setInterval(30);

		QObject::connect(timer, &QTimer::timeout, [] {
			POINT point;
			GetCursorPos(&point);

			auto stillPassing = false;
			for (auto& entry: masked()) {
				if (!entry.passing) continue;
				// Back in the mask, or off the window where its style doesn't matter.
				if (!outsideMask(entry, point)) setPassing(entry, false);
				else stillPassing = true;
			}

			if (!stillPassing) pointerPoll().stop();
		});

		return timer;
	}();

	return *timer;
}

void setPassing(MaskedWindow& entry, bool passing) {
	entry.passing = passing;

	auto* hwnd = hwndOf(entry.window);
	if (!hwnd) return;

	// Qt may have reset the style since the mask was set.
	if (passing) ensureLayered(hwnd);

	auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	auto updated = passing ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT;
	if (updated != style) SetWindowLongPtrW(hwnd, GWL_EXSTYLE, updated);

	if (passing) pointerPoll().start();
}

class HitTestFilter: public QAbstractNativeEventFilter {
public:
	bool nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* result) override {
		auto* msg = static_cast<MSG*>(message);
		if (msg->message != WM_NCHITTEST) return false;

		for (auto& entry: masked()) {
			if (hwndOf(entry.window) != msg->hwnd) continue;

			auto point = POINT {GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
			if (!outsideMask(entry, point)) return false;

			// Takes effect from the next pointer event; this one only reaches this thread's windows.
			setPassing(entry, true);
			*result = HTTRANSPARENT;
			return true;
		}

		return false;
	}
};

} // namespace

void applyInputMask(QWindow* window, const QRegion& mask) {
	static auto* filter = [] {
		auto* filter = new HitTestFilter(); // NOLINT
		QCoreApplication::instance()->installNativeEventFilter(filter);
		return filter;
	}();
	(void) filter;

	// Never a window region, which would clip the content.
	window->setMask(QRegion());

	auto& list = masked();
	list.removeIf([](const MaskedWindow& entry) { return !entry.window; });

	auto* entry = static_cast<MaskedWindow*>(nullptr);
	for (auto& e: list) {
		if (e.window == window) entry = &e;
	}

	if (mask.isEmpty()) {
		if (!entry) return;

		// Qt's own flag makes a window with an empty mask transparent, and owns the style then.
		if (entry->passing && !window->flags().testFlag(Qt::WindowTransparentForInput)) {
			setPassing(*entry, false);
		}

		list.removeIf([&](const MaskedWindow& e) { return e.window == window; });
		return;
	}

	if (!entry) {
		list.append({.window = window});
		entry = &list.last();
	}

	entry->mask = mask;
	if (auto* hwnd = hwndOf(window)) ensureLayered(hwnd);
}

} // namespace qs::windows
