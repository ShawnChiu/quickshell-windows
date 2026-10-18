// C++/WinRT comes before Qt, whose keyword macros it doesn't expect.
// clang-format off
#include <windows.h>

#include <d2d1effects.h>
#include <dispatcherqueue.h>
#include <dwmapi.h>
#include <windows.graphics.effects.interop.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.h>

#include "composition.hpp"
// clang-format on

#include <array>
#include <cstring>
#include <utility>
#include <vector>

#include <qabstractnativeeventfilter.h>
#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qcoreevent.h>
#include <qevent.h>
#include <qhash.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qrect.h>
#include <qregion.h>
#include <qtimer.h>
#include <qtypes.h>
#include <qwindow.h>

#include "../core/logcat.hpp"
#include "backdrop.hpp"
#include "wallpaper_blur.hpp"

namespace wuc = winrt::Windows::UI::Composition;
namespace wge = winrt::Windows::Graphics::Effects;
namespace abi_wge = ABI::Windows::Graphics::Effects;
using Inspectable = winrt::Windows::Foundation::IInspectable;
using winrt::Windows::Foundation::PropertyValue;

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logBackdrop, "quickshell.windows.backdrop", QtWarningMsg);

// Not in the SDK this builds with, and the SDK's GUID_POWER_SAVING_STATUS needs a library.
constexpr GUID BATTERY_SAVER_STATUS =
    {0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
constexpr GUID ENERGY_SAVER_STATUS =
    {0x550e8400, 0xe29b, 0x41d4, {0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00}};

// A Direct2D effect described for Windows.UI.Composition, the part of Win2D it needs.
// Properties are indexed by their Direct2D property index.
struct D2DEffect
    : winrt::implements<
          D2DEffect,
          wge::IGraphicsEffect,
          wge::IGraphicsEffectSource,
          abi_wge::IGraphicsEffectD2D1Interop> {
	D2DEffect(GUID id, std::vector<Inspectable> properties, wge::IGraphicsEffectSource source)
	    : id(id)
	    , properties(std::move(properties))
	    , source(std::move(source)) {}

	winrt::hstring Name() { return this->name; }
	void Name(const winrt::hstring& name) { this->name = name; }

	HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
		*id = this->id;
		return S_OK;
	}

	HRESULT __stdcall GetNamedPropertyMapping(
	    LPCWSTR /*name*/,
	    UINT* /*index*/,
	    abi_wge::GRAPHICS_EFFECT_PROPERTY_MAPPING* /*mapping*/
	) noexcept override {
		return E_INVALIDARG; // nothing is animated
	}

	HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
		*count = static_cast<UINT>(this->properties.size());
		return S_OK;
	}

	HRESULT __stdcall GetProperty(
	    UINT index,
	    ABI::Windows::Foundation::IPropertyValue** value
	) noexcept override {
		if (index >= this->properties.size()) return E_INVALIDARG;
		*value = this->properties[index].as<ABI::Windows::Foundation::IPropertyValue>().detach();
		return S_OK;
	}

	HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
		*count = 1;
		return S_OK;
	}

	HRESULT __stdcall GetSource(
	    UINT index,
	    abi_wge::IGraphicsEffectSource** source
	) noexcept override {
		if (index != 0) return E_INVALIDARG;
		*source = this->source.as<abi_wge::IGraphicsEffectSource>().detach();
		return S_OK;
	}

	GUID id;
	std::vector<Inspectable> properties;
	wge::IGraphicsEffectSource source;
	winrt::hstring name;
};

// Scales colour away from its luminance, as niri and CSS's saturate() do.
std::array<float, 20> saturationMatrix(float s) {
	const float r = 0.2126f, g = 0.7152f, b = 0.0722f;
	// clang-format off
	return {
		r + (1 - r) * s, r - r * s,       r - r * s,       0,
		g - g * s,       g + (1 - g) * s, g - g * s,       0,
		b - b * s,       b - b * s,       b + (1 - b) * s, 0,
		0,               0,               0,               1,
		0,               0,               0,               0,
	};
	// clang-format on
}

DWORD readPersonalize(const wchar_t* name, DWORD fallback) {
	DWORD value = 0;
	DWORD size = sizeof(value);

	auto status = RegGetValueW(
	    HKEY_CURRENT_USER,
	    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
	    name,
	    RRF_RT_REG_DWORD,
	    nullptr,
	    &value,
	    &size
	);

	return status == ERROR_SUCCESS ? value : fallback;
}

QHash<HWND, BlurBackdrop*>& targets() {
	static QHash<HWND, BlurBackdrop*> targets; // NOLINT
	return targets;
}

class TargetEventFilter: public QAbstractNativeEventFilter {
public:
	bool
	nativeEventFilter(const QByteArray& /*eventType*/, void* message, qintptr* /*result*/) override {
		auto* msg = static_cast<MSG*>(message);

		if (msg->message == WM_WINDOWPOSCHANGED) {
			if (auto* backdrop = targets().value(msg->hwnd)) backdrop->sync();
		}

		return false;
	}
};

// The rect of the window's monitor, in physical pixels.
QRect monitorRect(HWND hwnd) {
	auto info = MONITORINFO {.cbSize = sizeof(MONITORINFO)};
	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) return {};

	const auto& r = info.rcMonitor;
	return {r.left, r.top, r.right - r.left, r.bottom - r.top};
}

} // namespace

wuc::Compositor compositor() {
	// Leaked: these can't be released after COM shuts down at exit.
	static void* instance = [] {
		// Composition runs on a DispatcherQueue, which Qt's event loop pumps.
		if (!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
			auto options = DispatcherQueueOptions {
			    .dwSize = sizeof(DispatcherQueueOptions),
			    .threadType = DQTYPE_THREAD_CURRENT,
			    .apartmentType = DQTAT_COM_NONE,
			};

			ABI::Windows::System::IDispatcherQueueController* controller = nullptr;
			winrt::check_hresult(CreateDispatcherQueueController(options, &controller));
		}

		return winrt::detach_abi(wuc::Compositor());
	}();

	wuc::Compositor compositor {nullptr};
	winrt::copy_from_abi(compositor, instance);
	return compositor;
}

struct BlurBackdrop::Native {
	HWND hwnd = nullptr;
	// The Qt window.
	HWND target = nullptr;
	HPOWERNOTIFY batterySaverNotify = nullptr;
	HPOWERNOTIFY energySaverNotify = nullptr;
	bool batterySaver = false;
	bool energySaver = false;
	bool opaque = false;
	qint32 width = 0;
	qint32 height = 0;

	wuc::Desktop::DesktopWindowTarget compositionTarget {nullptr};
	wuc::SpriteVisual sprite {nullptr};
	wuc::CompositionMaskBrush mask {nullptr};
	wuc::ContainerVisual shape {nullptr};
	wuc::CompositionVisualSurface surface {nullptr};
	wuc::CompositionColorBrush white {nullptr};
	std::vector<wuc::SpriteVisual> rects;

	// The xray brush, showing the part of its monitor's blurred wallpaper under the window.
	wuc::CompositionSurfaceBrush xrayBrush {nullptr};
	void* xraySurface = nullptr;
	QRect xrayMonitor;

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

namespace {

const wchar_t* windowClass() {
	static const wchar_t* name = [] {
		const auto* name = L"QuickshellBlurBackdrop";
		auto wc = WNDCLASSW {};
		wc.lpfnWndProc = &BlurBackdrop::Native::windowProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = name;
		RegisterClassW(&wc);
		return name;
	}();

	return name;
}

} // namespace

LRESULT CALLBACK
BlurBackdrop::Native::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	auto* backdrop =
	    reinterpret_cast<BlurBackdrop*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); // NOLINT

	switch (msg) {
	case WM_NCHITTEST: return HTTRANSPARENT;
	case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
	case WM_SETTINGCHANGE:
		// Sent when "Transparency effects" or the app theme changes.
		if (backdrop && lParam != 0
		    && std::wcscmp(reinterpret_cast<const wchar_t*>(lParam), L"ImmersiveColorSet")
		           == 0) // NOLINT
		{
			backdrop->updateEffects();
		}
		break;
	case WM_POWERBROADCAST:
		if (backdrop && wParam == PBT_POWERSETTINGCHANGE) {
			const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam); // NOLINT
			if (setting->DataLength < sizeof(DWORD)) return TRUE;

			auto value = *reinterpret_cast<const DWORD*>(setting->Data); // NOLINT
			if (setting->PowerSetting == BATTERY_SAVER_STATUS) {
				backdrop->native->batterySaver = value != 0;
			} else if (setting->PowerSetting == ENERGY_SAVER_STATUS) {
				backdrop->native->energySaver = value != 0;
			}

			backdrop->updateEffects();
			return TRUE;
		}
		break;
	default: break;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

BlurBackdrop::BlurBackdrop(QWindow* window)
    : QObject(window)
    , window(window)
    , native(std::make_unique<Native>()) {
	static auto* filter = [] {
		auto* filter = new TargetEventFilter(); // NOLINT
		QCoreApplication::instance()->installNativeEventFilter(filter);
		return filter;
	}();
	(void) filter;

	window->installEventFilter(this);
	if (window->handle()) this->onSurfaceCreated();
}

BlurBackdrop::~BlurBackdrop() {
	this->onSurfaceDestroyed();

	auto* n = this->native.get();
	if (n->batterySaverNotify) UnregisterPowerSettingNotification(n->batterySaverNotify);
	if (n->energySaverNotify) UnregisterPowerSettingNotification(n->energySaverNotify);

	auto* hwnd = n->hwnd;
	*n = Native();

	if (hwnd) {
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
		DestroyWindow(hwnd);
	}
}

BlurBackdrop* BlurBackdrop::forWindow(QWindow* window) {
	if (auto* backdrop = window->findChild<BlurBackdrop*>(Qt::FindDirectChildrenOnly)) {
		return backdrop;
	}

	return new BlurBackdrop(window);
}

void BlurBackdrop::setRegion(const QRegion& region) {
	if (region == this->region) return;
	this->region = region;

	if (!region.isEmpty()) this->ensureCreated();
	this->updateShape();
	this->sync();
}

void BlurBackdrop::setRadius(qreal radius) {
	if (qFuzzyCompare(radius, this->radius)) return;
	this->radius = radius;
	this->updateBrush();
}

void BlurBackdrop::setSaturation(qreal saturation) {
	if (qFuzzyCompare(saturation, this->saturation)) return;
	this->saturation = saturation;
	this->updateBrush();
}

void BlurBackdrop::setXray(bool xray) {
	if (xray == this->xray) return;
	this->xray = xray;

	if (xray) {
		QObject::connect(
		    WallpaperBlurService::instance(),
		    &WallpaperBlurService::surfacesChanged,
		    this,
		    &BlurBackdrop::updateBrush,
		    Qt::UniqueConnection
		);
	} else {
		QObject::disconnect(WallpaperBlurService::instance(), nullptr, this, nullptr);
	}

	this->updateBrush();
}

void BlurBackdrop::claim(QObject* owner) { this->owner = owner; }

void BlurBackdrop::release(QObject* owner) {
	if (this->owner != owner) return;
	this->owner = nullptr;

	QTimer::singleShot(0, this, [this] {
		if (!this->owner) this->setRegion(QRegion());
	});
}

bool BlurBackdrop::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::PlatformSurface) {
		switch (static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType()) { // NOLINT
		case QPlatformSurfaceEvent::SurfaceCreated: this->onSurfaceCreated(); break;
		case QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed: this->onSurfaceDestroyed(); break;
		}
	}

	return this->QObject::eventFilter(watched, event);
}

void BlurBackdrop::onSurfaceCreated() {
	auto* target = reinterpret_cast<HWND>(this->window->winId()); // NOLINT
	this->native->target = target;
	targets().insert(target, this);
	this->sync();
}

void BlurBackdrop::onSurfaceDestroyed() {
	auto* n = this->native.get();
	if (!n->target) return;

	targets().remove(n->target);
	n->target = nullptr;
	this->sync();
}

void BlurBackdrop::ensureCreated() {
	auto* n = this->native.get();
	if (n->hwnd) return;

	// Layered and transparent so input passes through to whatever is behind,
	// as it would through the Qt window's own mask.
	n->hwnd = CreateWindowExW(
	    WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED
	        | WS_EX_TRANSPARENT,
	    windowClass(),
	    L"Quickshell blur",
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    GetModuleHandleW(nullptr),
	    nullptr
	);

	if (!n->hwnd) {
		qCWarning(logBackdrop) << "Could not create the blur window:" << GetLastError();
		return;
	}

	SetLayeredWindowAttributes(n->hwnd, 0, 255, LWA_ALPHA);
	SetWindowLongPtrW(n->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this)); // NOLINT

	auto corners = DWMWCP_DONOTROUND;
	DwmSetWindowAttribute(n->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

	// Each sends the current state right away.
	n->batterySaverNotify =
	    RegisterPowerSettingNotification(n->hwnd, &BATTERY_SAVER_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);
	n->energySaverNotify =
	    RegisterPowerSettingNotification(n->hwnd, &ENERGY_SAVER_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);

	try {
		auto compositor = qs::windows::compositor();
		auto interop =
		    compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();

		winrt::check_hresult(interop->CreateDesktopWindowTarget(
		    n->hwnd,
		    false,
		    reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>( // NOLINT
		        winrt::put_abi(n->compositionTarget)
		    )
		));

		// The blur fills the window and is masked by the region, drawn as white rects
		// into a surface. Blurring each rect on its own would blur them separately.
		n->white = compositor.CreateColorBrush(winrt::Windows::UI::Color {255, 255, 255, 255});
		n->shape = compositor.CreateContainerVisual();
		n->surface = compositor.CreateVisualSurface();
		n->surface.SourceVisual(n->shape);

		auto maskSurface = compositor.CreateSurfaceBrush(n->surface);
		maskSurface.Stretch(wuc::CompositionStretch::None);
		maskSurface.HorizontalAlignmentRatio(0);
		maskSurface.VerticalAlignmentRatio(0);

		n->mask = compositor.CreateMaskBrush();
		n->mask.Mask(maskSurface);

		n->sprite = compositor.CreateSpriteVisual();
		n->sprite.Brush(n->mask);
		n->compositionTarget.Root(n->sprite);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logBackdrop) << "Could not set up composition for blur:"
		                       << QString::fromWCharArray(e.message().c_str());
		n->compositionTarget = nullptr;
		n->sprite = nullptr;
		return;
	}

	this->updateEffects();
}

void BlurBackdrop::updateShape() {
	auto* n = this->native.get();
	if (!n->sprite) return;

	auto compositor = qs::windows::compositor();
	auto children = n->shape.Children();

	size_t i = 0;
	for (const auto& rect: this->region) {
		if (i == n->rects.size()) {
			auto sprite = compositor.CreateSpriteVisual();
			sprite.Brush(n->white);
			children.InsertAtTop(sprite);
			n->rects.push_back(sprite);
		}

		auto& sprite = n->rects[i++];
		sprite.Offset({static_cast<float>(rect.x()), static_cast<float>(rect.y()), 0});
		sprite.Size({static_cast<float>(rect.width()), static_cast<float>(rect.height())});
		sprite.IsVisible(true);
	}

	for (; i < n->rects.size(); i++) {
		n->rects[i].IsVisible(false);
	}
}

void BlurBackdrop::updateEffects() {
	auto* n = this->native.get();

	// Windows' own blur falls back to a solid colour in the same cases.
	auto transparency = readPersonalize(L"EnableTransparency", 1) != 0;
	auto opaque = !transparency || n->batterySaver || n->energySaver;

	qCDebug(logBackdrop) << "Transparency effects" << transparency << "battery saver"
	                     << n->batterySaver << "energy saver" << n->energySaver;

	n->opaque = opaque;
	this->updateBrush();
}

void BlurBackdrop::updateBrush() {
	auto* n = this->native.get();
	if (!n->mask) return;

	try {
		auto compositor = qs::windows::compositor();

		if (n->opaque) {
			auto light = readPersonalize(L"AppsUseLightTheme", 0) != 0;
			// The solid backgrounds Windows uses for Mica when it is off.
			auto color = light ? winrt::Windows::UI::Color {255, 0xf3, 0xf3, 0xf3}
			                   : winrt::Windows::UI::Color {255, 0x20, 0x20, 0x20};
			n->mask.Source(compositor.CreateColorBrush(color));
			return;
		}

		// Until the wallpaper's first blur is done, what is behind is blurred instead.
		if (this->xray && n->target) {
			auto monitor = monitorRect(n->target);
			auto* surface = WallpaperBlurService::instance()->surface(monitor);
			n->xrayMonitor = monitor;

			if (surface) {
				if (surface != n->xraySurface || !n->xrayBrush) {
					wuc::ICompositionSurface source {nullptr};
					winrt::copy_from_abi(source, surface);

					n->xrayBrush = compositor.CreateSurfaceBrush(source);
					n->xrayBrush.Stretch(wuc::CompositionStretch::None);
					n->xrayBrush.HorizontalAlignmentRatio(0);
					n->xrayBrush.VerticalAlignmentRatio(0);
					n->xraySurface = surface;
				}

				this->updateXrayOffset();
				n->mask.Source(n->xrayBrush);
				return;
			}
		}

		n->xrayBrush = nullptr;
		n->xraySurface = nullptr;

		wge::IGraphicsEffect effect = winrt::make<D2DEffect>(
		    CLSID_D2D1GaussianBlur,
		    std::vector<Inspectable> {
		        PropertyValue::CreateSingle(static_cast<float>(this->radius)),
		        PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED),
		        // Soft would fade the edges of the window to transparent.
		        PropertyValue::CreateUInt32(D2D1_BORDER_MODE_HARD),
		    },
		    wuc::CompositionEffectSourceParameter(L"backdrop")
		);

		if (!qFuzzyCompare(this->saturation, 1.0)) {
			auto matrix = saturationMatrix(static_cast<float>(this->saturation));

			effect = winrt::make<D2DEffect>(
			    CLSID_D2D1ColorMatrix,
			    std::vector<Inspectable> {
			        PropertyValue::CreateSingleArray(matrix),
			        PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED),
			        PropertyValue::CreateBoolean(true),
			    },
			    effect.as<wge::IGraphicsEffectSource>()
			);
		}

		auto brush = compositor.CreateEffectFactory(effect).CreateBrush();
		brush.SetSourceParameter(L"backdrop", compositor.CreateBackdropBrush());
		n->mask.Source(brush);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logBackdrop) << "Could not create the blur effect:"
		                       << QString::fromWCharArray(e.message().c_str());
	}
}

void BlurBackdrop::updateXrayOffset() {
	auto* n = this->native.get();
	if (!n->xrayBrush || !n->target) return;

	RECT rect;
	GetWindowRect(n->target, &rect);

	n->xrayBrush.Offset({
	    static_cast<float>(n->xrayMonitor.left() - rect.left),
	    static_cast<float>(n->xrayMonitor.top() - rect.top),
	});
}

void BlurBackdrop::sync() {
	auto* n = this->native.get();
	if (!n->hwnd) return;

	auto shown =
	    n->target && !this->region.isEmpty() && IsWindowVisible(n->target) && !IsIconic(n->target);

	if (!shown) {
		if (IsWindowVisible(n->hwnd)) ShowWindow(n->hwnd, SW_HIDE);
		return;
	}

	RECT rect;
	GetWindowRect(n->target, &rect);
	auto width = static_cast<qint32>(rect.right - rect.left);
	auto height = static_cast<qint32>(rect.bottom - rect.top);

	if (n->sprite && (width != n->width || height != n->height)) {
		auto size = winrt::Windows::Foundation::Numerics::float2 {
		    static_cast<float>(width),
		    static_cast<float>(height),
		};

		n->sprite.Size(size);
		n->surface.SourceSize(size);
		n->width = width;
		n->height = height;
	}

	if (this->xray) {
		if (monitorRect(n->target) != n->xrayMonitor) this->updateBrush();
		else this->updateXrayOffset();
	}

	// Into the Qt window's band first: placing it after a topmost window doesn't make it topmost.
	auto topmost = (GetWindowLongPtrW(n->target, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
	if (topmost != ((GetWindowLongPtrW(n->hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0)) {
		SetWindowPos(
		    n->hwnd,
		    topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
		    0,
		    0,
		    0,
		    0,
		    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
		);
	}

	// Just below the Qt window.
	SetWindowPos(
	    n->hwnd,
	    n->target,
	    rect.left,
	    rect.top,
	    width,
	    height,
	    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW
	);
}

} // namespace qs::windows
