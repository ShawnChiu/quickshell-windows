#include "panel_window.hpp"

#include <qabstractnativeeventfilter.h>
#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qevent.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetaobject.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qqmlengine.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qscreen.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwindow.h>

// windows.h has to come before shellapi.h
#include <windows.h>

#include <shellapi.h>

#include "../core/logcat.hpp"
#include "../core/qmlscreen.hpp"
#include "../core/types.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"

namespace {

QS_LOGGING_CATEGORY(logPanel, "quickshell.windows.panel", QtWarningMsg);

UINT appBarCallbackMessage() {
	static const UINT message = RegisterWindowMessageW(L"QuickshellAppBarNotify");
	return message;
}

QHash<HWND, WinPanelWindow*>& registeredAppBars() {
	static QHash<HWND, WinPanelWindow*> appBars; // NOLINT
	return appBars;
}

// Qt's high dpi scaling keeps each screen's top left corner at its native position
// and scales everything else relative to it.
QPoint toNative(QPoint point, QScreen* screen) {
	auto origin = screen->geometry().topLeft();
	auto dpr = screen->devicePixelRatio();
	return origin + QPoint(qRound((point.x() - origin.x()) * dpr), qRound((point.y() - origin.y()) * dpr));
}

QPoint fromNative(QPoint point, QScreen* screen) {
	auto origin = screen->geometry().topLeft();
	auto dpr = screen->devicePixelRatio();
	return origin + QPoint(qRound((point.x() - origin.x()) / dpr), qRound((point.y() - origin.y()) / dpr));
}

RECT toNativeRect(const QRect& rect, QScreen* screen) {
	auto topLeft = toNative(rect.topLeft(), screen);
	auto bottomRight = toNative(rect.topLeft() + QPoint(rect.width(), rect.height()), screen);
	return RECT {
	    .left = topLeft.x(),
	    .top = topLeft.y(),
	    .right = bottomRight.x(),
	    .bottom = bottomRight.y(),
	};
}

QRect fromNativeRect(const RECT& rect, QScreen* screen) {
	auto topLeft = fromNative(QPoint(rect.left, rect.top), screen);
	auto bottomRight = fromNative(QPoint(rect.right, rect.bottom), screen);
	return QRect(topLeft, QSize(bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y()));
}

UINT appBarEdge(Qt::Edge edge) {
	switch (edge) {
	case Qt::LeftEdge: return ABE_LEFT;
	case Qt::RightEdge: return ABE_RIGHT;
	case Qt::TopEdge: return ABE_TOP;
	case Qt::BottomEdge: return ABE_BOTTOM;
	}

	return ABE_TOP;
}

// Fixes the thickness of the AppBar rect from its anchored edge.
void setAppBarThickness(RECT& rect, Qt::Edge edge, LONG thickness) {
	switch (edge) {
	case Qt::LeftEdge: rect.right = rect.left + thickness; break;
	case Qt::RightEdge: rect.left = rect.right - thickness; break;
	case Qt::TopEdge: rect.bottom = rect.top + thickness; break;
	case Qt::BottomEdge: rect.top = rect.bottom - thickness; break;
	}
}

class AppBarNativeEventFilter: public QAbstractNativeEventFilter {
public:
	bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* /*result*/) override {
		if (eventType != "windows_generic_MSG") return false;

		auto* msg = static_cast<MSG*>(message);
		auto* panel = registeredAppBars().value(msg->hwnd);
		if (panel == nullptr) return false;

		if (msg->message == appBarCallbackMessage()) {
			switch (msg->wParam) {
			case ABN_POSCHANGED: panel->onAppBarPositionChanged(); break;
			case ABN_FULLSCREENAPP: panel->onFullscreenAppChanged(msg->lParam != 0); break;
			default: break;
			}
		} else if (msg->message == WM_ACTIVATE || msg->message == WM_WINDOWPOSCHANGED) {
			// Required by the AppBar protocol so the shell can keep its z-order bookkeeping.
			APPBARDATA data {};
			data.cbSize = sizeof(data);
			data.hWnd = msg->hwnd;
			SHAppBarMessage(msg->message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
		}

		return false;
	}
};

} // namespace

bool WinPanelEventFilter::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::WindowStateChange) {
		// Like a layer shell surface, a panel can't be minimized. Window managers minimize the
		// foreground window without asking (komorebic minimize), so undo it.
		auto* window = static_cast<QWindow*>(watched); // NOLINT
		if (window->windowStates().testFlag(Qt::WindowMinimized)) {
			auto* hwnd = reinterpret_cast<HWND>(window->winId()); // NOLINT
			QMetaObject::invokeMethod(
			    window,
			    [hwnd] { ShowWindow(hwnd, SW_SHOWNOACTIVATE); },
			    Qt::QueuedConnection
			);
		}
	} else if (event->type() == QEvent::PlatformSurface) {
		auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event); // NOLINT

		switch (surfaceEvent->surfaceEventType()) {
		case QPlatformSurfaceEvent::SurfaceCreated: emit this->surfaceCreated(); break;
		case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed:
			emit this->surfaceAboutToBeDestroyed();
			break;
		}
	}

	return this->QObject::eventFilter(watched, event);
}

void WinPanelWindow::installNativeEventFilter() {
	static auto* filter = new AppBarNativeEventFilter(); // NOLINT
	QCoreApplication::instance()->installNativeEventFilter(filter);
}

WinPanelWindow::WinPanelWindow(QObject* parent): ProxyWindowBase(parent) {
	QObject::connect(
	    &this->eventFilter,
	    &WinPanelEventFilter::surfaceCreated,
	    this,
	    &WinPanelWindow::updateDimensionsSlot
	);

	QObject::connect(
	    &this->eventFilter,
	    &WinPanelEventFilter::surfaceAboutToBeDestroyed,
	    this,
	    &WinPanelWindow::removeAppBar
	);

	this->bcExclusiveZone.setBinding([this]() -> qint32 {
		switch (this->bExclusionMode.value()) {
		case ExclusionMode::Ignore: return 0;
		case ExclusionMode::Normal: return this->bExclusiveZone;
		case ExclusionMode::Auto:
			auto edge = this->bcExclusionEdge.value();
			auto margins = this->bMargins.value();

			if (edge == Qt::TopEdge || edge == Qt::BottomEdge) {
				return this->bImplicitHeight + margins.top + margins.bottom;
			} else if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
				return this->bImplicitWidth + margins.left + margins.right;
			} else {
				return 0;
			}
		}

		return 0;
	});

	this->bcExclusionEdge.setBinding([this] { return this->bAnchors.value().exclusionEdge(); });
}

WinPanelWindow::~WinPanelWindow() { this->removeAppBar(); }

void WinPanelWindow::connectWindow() {
	this->ProxyWindowBase::connectWindow();

	this->window->installEventFilter(&this->eventFilter);
	this->updateScreen();

	QObject::connect(
	    this->window,
	    &QQuickWindow::visibleChanged,
	    this,
	    &WinPanelWindow::updateDimensionsSlot
	);

	// Tool windows are kept out of the taskbar and alt-tab.
	this->window->setFlag(Qt::FramelessWindowHint);
	this->window->setFlag(Qt::Tool);
	this->updateAboveWindows();
	this->updateFocusable();

	if (this->window->handle() != nullptr) {
		this->updateDimensions();
	}
}

ProxiedWindow* WinPanelWindow::disownWindow(bool keepItemOwnership) {
	// The AppBar is bound to the window handle. If the window is passed to a reloaded panel
	// it will register it again.
	this->removeAppBar();
	if (this->window != nullptr) this->window->removeEventFilter(&this->eventFilter);
	return this->ProxyWindowBase::disownWindow(keepItemOwnership);
}

void WinPanelWindow::trySetWidth(qint32 implicitWidth) {
	// only update the actual size if not blocked by anchors
	if (!this->bAnchors.value().horizontalConstraint()) {
		this->ProxyWindowBase::trySetWidth(implicitWidth);
		this->updateDimensions();
	}
}

void WinPanelWindow::trySetHeight(qint32 implicitHeight) {
	// only update the actual size if not blocked by anchors
	if (!this->bAnchors.value().verticalConstraint()) {
		this->ProxyWindowBase::trySetHeight(implicitHeight);
		this->updateDimensions();
	}
}

void WinPanelWindow::setScreen(QuickshellScreenInfo* screen) {
	this->ProxyWindowBase::setScreen(screen);
	this->updateScreen();
}

void WinPanelWindow::updateScreen() {
	auto* newScreen =
	    this->mScreen ? this->mScreen : (this->window ? this->window->screen() : nullptr);

	if (newScreen == this->mTrackedScreen) return;

	if (this->mTrackedScreen != nullptr) {
		QObject::disconnect(this->mTrackedScreen, nullptr, this, nullptr);
	}

	// AppBars are positioned per monitor, so moving screens needs a fresh position.
	this->removeAppBar();
	this->mTrackedScreen = newScreen;

	if (this->mTrackedScreen != nullptr) {
		QObject::connect(
		    this->mTrackedScreen,
		    &QScreen::geometryChanged,
		    this,
		    &WinPanelWindow::updateDimensionsSlot
		);

		QObject::connect(
		    this->mTrackedScreen,
		    &QScreen::physicalDotsPerInchChanged,
		    this,
		    &WinPanelWindow::updateDimensionsSlot
		);

		// Panels without an AppBar of their own are placed in the work area,
		// which changes when the taskbar or other AppBars move.
		QObject::connect(
		    this->mTrackedScreen,
		    &QScreen::availableGeometryChanged,
		    this,
		    &WinPanelWindow::updateDimensionsSlot
		);
	}

	this->updateDimensions();
}

void WinPanelWindow::updateDimensionsSlot() { this->updateDimensions(); }

void WinPanelWindow::onAppBarPositionChanged() {
	// Notifications arrive inside SendMessage calls made by the shell,
	// so defer the update instead of calling back into it.
	if (this->dimensionsUpdateQueued) return;
	this->dimensionsUpdateQueued = true;

	QMetaObject::invokeMethod(
	    this,
	    [this]() {
		    this->dimensionsUpdateQueued = false;
		    this->updateDimensions();
	    },
	    Qt::QueuedConnection
	);
}

void WinPanelWindow::onFullscreenAppChanged(bool fullscreen) {
	if (fullscreen == this->fullscreenAppActive) return;
	this->fullscreenAppActive = fullscreen;

	if (this->window == nullptr || !this->bAboveWindows) return;

	// Drop out of the topmost band while a fullscreen app (e.g. a game or video) is open.
	auto* hwnd = reinterpret_cast<HWND>(this->window->winId()); // NOLINT
	SetWindowPos(
	    hwnd,
	    fullscreen ? HWND_BOTTOM : HWND_TOPMOST,
	    0,
	    0,
	    0,
	    0,
	    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
	);
}

void WinPanelWindow::updateDimensions() {
	if (this->window == nullptr || this->window->handle() == nullptr
	    || this->mTrackedScreen == nullptr)
		return;

	auto screenGeometry = this->mTrackedScreen->geometry();

	auto edge = this->bcExclusionEdge.value();
	auto exclusiveZone = this->bcExclusiveZone.value();
	auto exclusionMode = this->bExclusionMode.value();

	auto wantsAppBar = this->window->isVisible() && edge != 0
	                && exclusionMode != ExclusionMode::Ignore && exclusiveZone > 0;

	if (wantsAppBar) {
		screenGeometry = this->reserveAppBar(screenGeometry, edge, exclusiveZone);
	} else {
		this->removeAppBar();

		// Without an AppBar the closest match to respecting other panels is the work area,
		// which excludes the taskbar and all registered AppBars.
		if (exclusionMode != ExclusionMode::Ignore) {
			screenGeometry = this->mTrackedScreen->availableGeometry();
		}
	}

	auto geometry = QRect();

	auto anchors = this->bAnchors.value();
	auto margins = this->bMargins.value();

	if (anchors.horizontalConstraint()) {
		geometry.setX(screenGeometry.x() + margins.left);
		geometry.setWidth(screenGeometry.width() - margins.left - margins.right);
	} else {
		if (anchors.mLeft) {
			geometry.setX(screenGeometry.x() + margins.left);
		} else if (anchors.mRight) {
			geometry.setX(
			    screenGeometry.x() + screenGeometry.width() - this->implicitWidth() - margins.right
			);
		} else {
			geometry.setX(screenGeometry.x() + screenGeometry.width() / 2 - this->implicitWidth() / 2);
		}

		geometry.setWidth(this->implicitWidth());
	}

	if (anchors.verticalConstraint()) {
		geometry.setY(screenGeometry.y() + margins.top);
		geometry.setHeight(screenGeometry.height() - margins.top - margins.bottom);
	} else {
		if (anchors.mTop) {
			geometry.setY(screenGeometry.y() + margins.top);
		} else if (anchors.mBottom) {
			geometry.setY(
			    screenGeometry.y() + screenGeometry.height() - this->implicitHeight() - margins.bottom
			);
		} else {
			geometry.setY(screenGeometry.y() + screenGeometry.height() / 2 - this->implicitHeight() / 2);
		}

		geometry.setHeight(this->implicitHeight());
	}

	this->window->setGeometry(geometry);
}

QRect WinPanelWindow::reserveAppBar(QRect screenGeometry, Qt::Edge edge, qint32 exclusiveZone) {
	auto* screen = this->mTrackedScreen.data();
	auto* hwnd = reinterpret_cast<HWND>(this->window->winId()); // NOLINT

	if (this->appBarHwnd != reinterpret_cast<quintptr>(hwnd)) {
		this->removeAppBar();

		// Registration failing is not transient, don't retry (and warn) on every update.
		if (this->appBarFailedHwnd == reinterpret_cast<quintptr>(hwnd)) return screenGeometry;

		APPBARDATA data {};
		data.cbSize = sizeof(data);
		data.hWnd = hwnd;
		data.uCallbackMessage = appBarCallbackMessage();

		if (!SHAppBarMessage(ABM_NEW, &data)) {
			qCWarning(logPanel).nospace()
			    << "Failed to register AppBar for " << this << " (error " << GetLastError()
			    << "), the panel will not reserve screen space. This happens when another program "
			       "running as administrator manages the taskbar.";
			this->appBarFailedHwnd = reinterpret_cast<quintptr>(hwnd);
			return screenGeometry;
		}

		this->appBarHwnd = reinterpret_cast<quintptr>(hwnd);
		registeredAppBars().insert(hwnd, this);
		qCDebug(logPanel) << "Registered AppBar for" << this;
	}

	auto thickness = static_cast<LONG>(qRound(exclusiveZone * screen->devicePixelRatio()));

	APPBARDATA data {};
	data.cbSize = sizeof(data);
	data.hWnd = hwnd;
	data.uEdge = appBarEdge(edge);
	data.rc = toNativeRect(screenGeometry, screen);
	setAppBarThickness(data.rc, edge, thickness);

	// The shell moves the proposed rect out of the way of the taskbar and other AppBars,
	// which may also change its thickness.
	SHAppBarMessage(ABM_QUERYPOS, &data);
	setAppBarThickness(data.rc, edge, thickness);

	auto reserved = QRect(
	    QPoint(data.rc.left, data.rc.top),
	    QSize(data.rc.right - data.rc.left, data.rc.bottom - data.rc.top)
	);

	// Setting the position notifies every other AppBar, so skip it if nothing changed to
	// avoid ping-ponging notifications between panels.
	if (reserved != this->lastAppBarRect || edge != this->lastAppBarEdge) {
		SHAppBarMessage(ABM_SETPOS, &data);
		this->lastAppBarRect = reserved;
		this->lastAppBarEdge = edge;
	}

	auto logicalReserved = fromNativeRect(data.rc, screen);

	switch (edge) {
	case Qt::LeftEdge: screenGeometry.setLeft(logicalReserved.left()); break;
	case Qt::RightEdge: screenGeometry.setRight(logicalReserved.right()); break;
	case Qt::TopEdge: screenGeometry.setTop(logicalReserved.top()); break;
	case Qt::BottomEdge: screenGeometry.setBottom(logicalReserved.bottom()); break;
	}

	return screenGeometry;
}

void WinPanelWindow::removeAppBar() {
	if (this->appBarHwnd == 0) return;

	auto* hwnd = reinterpret_cast<HWND>(this->appBarHwnd); // NOLINT

	APPBARDATA data {};
	data.cbSize = sizeof(data);
	data.hWnd = hwnd;
	SHAppBarMessage(ABM_REMOVE, &data);

	registeredAppBars().remove(hwnd);
	this->appBarHwnd = 0;
	this->lastAppBarRect = QRect();
	this->lastAppBarEdge = static_cast<Qt::Edge>(0);
	qCDebug(logPanel) << "Removed AppBar for" << this;
}

void WinPanelWindow::updateAboveWindows() {
	if (this->window == nullptr) return;

	auto above = this->bAboveWindows.value();
	this->window->setFlag(Qt::WindowStaysOnBottomHint, !above);
	this->window->setFlag(Qt::WindowStaysOnTopHint, above);
}

void WinPanelWindow::updateFocusable() {
	if (this->window == nullptr) return;
	this->window->setFlag(Qt::WindowDoesNotAcceptFocus, !this->bFocusable);
}

// WinPanelInterface

WinPanelInterface::WinPanelInterface(QObject* parent)
    : PanelWindowInterface(parent)
    , panel(new WinPanelWindow(this)) {
	this->connectSignals();

	// clang-format off
	QObject::connect(this->panel, &WinPanelWindow::anchorsChanged, this, &WinPanelInterface::anchorsChanged);
	QObject::connect(this->panel, &WinPanelWindow::marginsChanged, this, &WinPanelInterface::marginsChanged);
	QObject::connect(this->panel, &WinPanelWindow::exclusiveZoneChanged, this, &WinPanelInterface::exclusiveZoneChanged);
	QObject::connect(this->panel, &WinPanelWindow::exclusionModeChanged, this, &WinPanelInterface::exclusionModeChanged);
	QObject::connect(this->panel, &WinPanelWindow::aboveWindowsChanged, this, &WinPanelInterface::aboveWindowsChanged);
	QObject::connect(this->panel, &WinPanelWindow::focusableChanged, this, &WinPanelInterface::focusableChanged);
	// clang-format on
}

void WinPanelInterface::onReload(QObject* oldInstance) {
	QQmlEngine::setContextForObject(this->panel, QQmlEngine::contextForObject(this));

	auto* old = qobject_cast<WinPanelInterface*>(oldInstance);
	this->panel->reload(old != nullptr ? old->panel : nullptr);
}

ProxyWindowBase* WinPanelInterface::proxyWindow() const { return this->panel; }

// NOLINTBEGIN
#define proxyPair(type, get, set)                                                                  \
	type WinPanelInterface::get() const { return this->panel->get(); }                               \
	void WinPanelInterface::set(type value) { this->panel->set(value); }

proxyPair(Anchors, anchors, setAnchors);
proxyPair(Margins, margins, setMargins);
proxyPair(qint32, exclusiveZone, setExclusiveZone);
proxyPair(ExclusionMode::Enum, exclusionMode, setExclusionMode);
proxyPair(bool, focusable, setFocusable);
proxyPair(bool, aboveWindows, setAboveWindows);

#undef proxyPair
// NOLINTEND
