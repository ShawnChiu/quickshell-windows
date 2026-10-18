// C++/WinRT comes before Qt, whose keyword macros it doesn't expect.
// clang-format off
#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <shobjidl_core.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.UI.Composition.h>

#include "composition.hpp"
// clang-format on

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <qbrush.h>
#include <qcolor.h>
#include <qdatetime.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpainter.h>
#include <qrect.h>
#include <qstring.h>
#include <qthreadpool.h>
#include <qtimer.h>
#include <qtypes.h>

#include "../core/logcat.hpp"
#include "wallpaper_blur.hpp"

namespace wuc = winrt::Windows::UI::Composition;
namespace abi_wuc = ABI::Windows::UI::Composition;

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logWallpaperBlur, "quickshell.windows.wallpaperblur", QtWarningMsg);

// What Windows draws on one monitor, enough to tell when it changes.
struct MonitorWallpaper {
	QRect rect;
	// The rect the wallpaper is fitted into: the monitor's, or every monitor's when spanned.
	QRect fitRect;
	QString path;
	QDateTime modified;
	DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
	QColor background;

	bool operator==(const MonitorWallpaper& other) const = default;
};

struct BlurParams {
	qint32 passes = 3;
	qreal offset = 3;
	qreal saturation = 1.5;
	qreal noise = 0.02;

	bool operator==(const BlurParams& other) const = default;
};

// An RGB image in floats, for the blur's passes.
struct Plane {
	qint32 width = 0;
	qint32 height = 0;
	std::vector<float> data;

	Plane(qint32 width, qint32 height)
	    : width(width)
	    , height(height)
	    , data(static_cast<size_t>(width) * height * 3) {}

	float* at(qint32 x, qint32 y) {
		return &this->data[(static_cast<size_t>(y) * this->width + x) * 3];
	}

	// Bilinear, clamped to the edge, at normalized coordinates: a GL texture's LINEAR and
	// CLAMP_TO_EDGE.
	void sample(float u, float v, float* out) const {
		auto fx = (u * static_cast<float>(this->width)) - 0.5f;
		auto fy = (v * static_cast<float>(this->height)) - 0.5f;
		auto x0f = std::floor(fx);
		auto y0f = std::floor(fy);
		auto tx = fx - x0f;
		auto ty = fy - y0f;

		auto clampX = [&](qint32 x) { return std::clamp(x, 0, this->width - 1); };
		auto clampY = [&](qint32 y) { return std::clamp(y, 0, this->height - 1); };
		auto x0 = clampX(static_cast<qint32>(x0f));
		auto x1 = clampX(static_cast<qint32>(x0f) + 1);
		auto y0 = clampY(static_cast<qint32>(y0f));
		auto y1 = clampY(static_cast<qint32>(y0f) + 1);

		const auto* p00 = &this->data[(static_cast<size_t>(y0) * this->width + x0) * 3];
		const auto* p10 = &this->data[(static_cast<size_t>(y0) * this->width + x1) * 3];
		const auto* p01 = &this->data[(static_cast<size_t>(y1) * this->width + x0) * 3];
		const auto* p11 = &this->data[(static_cast<size_t>(y1) * this->width + x1) * 3];

		for (auto c = 0; c != 3; c++) {
			auto top = p00[c] + ((p10[c] - p00[c]) * tx);
			auto bottom = p01[c] + ((p11[c] - p01[c]) * tx);
			out[c] = top + ((bottom - top) * ty);
		}
	}

	// niri's textures are 8 bit, so every pass rounds.
	void quantize() {
		for (auto& v: this->data) v = std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f) / 255.0f;
	}
};

// niri's blur_down.frag: half_pixel is half of a destination pixel.
Plane downsample(const Plane& src, float offset) {
	auto dst = Plane(std::max(1, src.width / 2), std::max(1, src.height / 2));
	auto ox = 0.5f / static_cast<float>(dst.width) * offset;
	auto oy = 0.5f / static_cast<float>(dst.height) * offset;

	float s[3];
	for (auto y = 0; y != dst.height; y++) {
		auto v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dst.height);
		for (auto x = 0; x != dst.width; x++) {
			auto u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dst.width);
			auto* out = dst.at(x, y);

			src.sample(u, v, s);
			for (auto c = 0; c != 3; c++) out[c] = s[c] * 4;

			const std::pair<float, float> corners[] = {{-ox, -oy}, {ox, -oy}, {-ox, oy}, {ox, oy}};
			for (auto [dx, dy]: corners) {
				src.sample(u + dx, v + dy, s);
				for (auto c = 0; c != 3; c++) out[c] += s[c];
			}

			for (auto c = 0; c != 3; c++) out[c] /= 8;
		}
	}

	dst.quantize();
	return dst;
}

// niri's blur_up.frag: half_pixel is half of a source pixel.
Plane upsample(const Plane& src, qint32 width, qint32 height, float offset) {
	auto dst = Plane(width, height);
	auto ox = 0.5f / static_cast<float>(src.width) * offset;
	auto oy = 0.5f / static_cast<float>(src.height) * offset;

	const std::pair<std::pair<float, float>, float> taps[] = {
	    {{-ox * 2, 0}, 1},
	    {{ox * 2, 0}, 1},
	    {{0, -oy * 2}, 1},
	    {{0, oy * 2}, 1},
	    {{-ox, oy}, 2},
	    {{ox, oy}, 2},
	    {{-ox, -oy}, 2},
	    {{ox, -oy}, 2},
	};

	float s[3];
	for (auto y = 0; y != dst.height; y++) {
		auto v = (static_cast<float>(y) + 0.5f) / static_cast<float>(dst.height);
		for (auto x = 0; x != dst.width; x++) {
			auto u = (static_cast<float>(x) + 0.5f) / static_cast<float>(dst.width);
			auto* out = dst.at(x, y);
			out[0] = out[1] = out[2] = 0;

			for (const auto& [d, weight]: taps) {
				src.sample(u + d.first, v + d.second, s);
				for (auto c = 0; c != 3; c++) out[c] += s[c] * weight;
			}

			for (auto c = 0; c != 3; c++) out[c] /= 12;
		}
	}

	dst.quantize();
	return dst;
}

// niri's postprocess.frag noise: sin-less white noise by David Hoskins (MIT License).
float hash12(float px, float py) {
	auto fract = [](float v) { return v - std::floor(v); };
	auto x = fract(px * 0.1031f);
	auto y = fract(py * 0.1031f);
	auto z = fract(px * 0.1031f);
	auto d = (x * (y + 33.33f)) + (y * (z + 33.33f)) + (z * (x + 33.33f));
	x += d;
	y += d;
	z += d;
	return fract((x + y) * z);
}

// The wallpaper as Windows lays it out on the monitor.
QImage drawWallpaper(const MonitorWallpaper& monitor) {
	auto image = QImage(monitor.rect.size(), QImage::Format_RGB32);
	image.fill(monitor.background);

	auto wallpaper = monitor.path.isEmpty() ? QImage() : QImage(monitor.path);
	if (wallpaper.isNull()) return image;

	auto painter = QPainter(&image);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	// Into the monitor's coordinates.
	painter.translate(-(monitor.rect.topLeft() - monitor.fitRect.topLeft()));

	auto area = QRectF(QPointF(0, 0), monitor.fitRect.size());
	auto size = QSizeF(wallpaper.size());

	switch (monitor.position) {
	case DWPOS_TILE:
		painter.fillRect(area, QBrush(wallpaper));
		painter.end();
		return image;
	case DWPOS_STRETCH: break;
	case DWPOS_CENTER:
		area = QRectF(area.center() - QPointF(size.width(), size.height()) / 2, size);
		break;
	case DWPOS_FIT:
		size.scale(area.size(), Qt::KeepAspectRatio);
		area = QRectF(area.center() - QPointF(size.width(), size.height()) / 2, size);
		break;
	default: // fill and span
		size.scale(area.size(), Qt::KeepAspectRatioByExpanding);
		area = QRectF(area.center() - QPointF(size.width(), size.height()) / 2, size);
		break;
	}

	painter.drawImage(area, wallpaper);
	painter.end();
	return image;
}

// niri's blur and postprocess, for one output.
QImage blurWallpaper(const QImage& wallpaper, const BlurParams& params) {
	auto width = wallpaper.width();
	auto height = wallpaper.height();

	auto source = Plane(width, height);
	for (auto y = 0; y != height; y++) {
		const auto* line = reinterpret_cast<const QRgb*>(wallpaper.constScanLine(y)); // NOLINT
		for (auto x = 0; x != width; x++) {
			auto* out = source.at(x, y);
			out[0] = static_cast<float>(qRed(line[x])) / 255;
			out[1] = static_cast<float>(qGreen(line[x])) / 255;
			out[2] = static_cast<float>(qBlue(line[x])) / 255;
		}
	}

	auto offset = static_cast<float>(params.offset);
	auto passes = std::clamp(params.passes, 1, 31);

	std::vector<Plane> levels;
	levels.push_back(std::move(source));
	for (auto i = 0; i != passes; i++) levels.push_back(downsample(levels.back(), offset));

	for (auto i = passes; i != 0; i--) {
		const auto& dst = levels[i - 1];
		levels[i - 1] = upsample(levels[i], dst.width, dst.height, offset);
	}

	const auto& blurred = levels.front();
	auto saturation = static_cast<float>(params.saturation);
	auto noise = static_cast<float>(params.noise);

	auto result = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
	for (auto y = 0; y != height; y++) {
		auto* line = reinterpret_cast<QRgb*>(result.scanLine(y)); // NOLINT
		for (auto x = 0; x != width; x++) {
			const auto* p = &blurred.data[(static_cast<size_t>(y) * width + x) * 3];
			float rgb[3] = {p[0], p[1], p[2]};

			if (saturation != 1) {
				auto luma = (rgb[0] * 0.2126f) + (rgb[1] * 0.7152f) + (rgb[2] * 0.0722f);
				for (auto& c: rgb) c = luma + ((c - luma) * saturation);
			}

			if (noise > 0) {
				// gl_FragCoord: pixel centres, from the bottom.
				auto n =
				    (hash12(static_cast<float>(x) + 0.5f, static_cast<float>(height - y) - 0.5f) - 0.5f)
				    * noise;
				for (auto& c: rgb) c += n;
			}

			auto channel = [](float v) {
				return static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * 255));
			};

			line[x] = qRgb(channel(rgb[0]), channel(rgb[1]), channel(rgb[2]));
		}
	}

	return result;
}

QRect toQRect(const RECT& r) { return {r.left, r.top, r.right - r.left, r.bottom - r.top}; }

// Every attached monitor's wallpaper, from the API Windows itself uses.
std::vector<MonitorWallpaper> readWallpapers() {
	std::vector<MonitorWallpaper> monitors;

	try {
		// Served by Explorer, out of process.
		auto wallpaper =
		    winrt::create_instance<IDesktopWallpaper>(CLSID_DesktopWallpaper, CLSCTX_LOCAL_SERVER);

		auto position = DWPOS_FILL;
		wallpaper->GetPosition(&position);

		COLORREF background = 0;
		wallpaper->GetBackgroundColor(&background);

		UINT count = 0;
		winrt::check_hresult(wallpaper->GetMonitorDevicePathCount(&count));

		QRect all;
		for (UINT i = 0; i != count; i++) {
			LPWSTR id = nullptr;
			if (FAILED(wallpaper->GetMonitorDevicePathAt(i, &id))) continue;

			RECT rect;
			// S_FALSE for monitors that are not attached.
			if (wallpaper->GetMonitorRECT(id, &rect) != S_OK) {
				CoTaskMemFree(id);
				continue;
			}

			LPWSTR path = nullptr;
			auto monitor = MonitorWallpaper {
			    .rect = toQRect(rect),
			    .position = position,
			    .background = QColor(GetRValue(background), GetGValue(background), GetBValue(background)),
			};

			if (SUCCEEDED(wallpaper->GetWallpaper(id, &path)) && path) {
				monitor.path = QString::fromWCharArray(path);
				monitor.modified = QFileInfo(monitor.path).lastModified();
				CoTaskMemFree(path);
			}

			CoTaskMemFree(id);

			if (monitor.rect.isEmpty()) continue;
			all |= monitor.rect;
			monitors.push_back(std::move(monitor));
		}

		for (auto& monitor: monitors) {
			monitor.fitRect = position == DWPOS_SPAN ? all : monitor.rect;
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWallpaperBlur) << "Could not read the wallpaper:"
		                            << QString::fromWCharArray(e.message().c_str());
	}

	return monitors;
}

const wchar_t* windowClass();

} // namespace

struct WallpaperBlurService::Native {
	struct Monitor {
		MonitorWallpaper wallpaper;
		BlurParams params;
		// Kept to draw again when the rendering device is replaced.
		QImage blurred;
		wuc::CompositionDrawingSurface surface {nullptr};
		// What backdrops are handed, see WallpaperBlurService::surface.
		wuc::ICompositionSurface source {nullptr};
	};

	// Gets setting changes, which only top level windows do.
	HWND hwnd = nullptr;
	QTimer poll;
	std::vector<Monitor> monitors;
	quint64 generation = 0;

	winrt::com_ptr<ID3D11Device> d3dDevice;
	winrt::com_ptr<ID2D1Device> d2dDevice;
	wuc::CompositionGraphicsDevice graphicsDevice {nullptr};

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool createDevice();
	bool ensureGraphicsDevice();
	void draw(Monitor& monitor);
	void drawAll();
};

namespace {

const wchar_t* windowClass() {
	static const wchar_t* name = [] {
		const auto* name = L"QuickshellWallpaperBlur";
		auto wc = WNDCLASSW {};
		wc.lpfnWndProc = &WallpaperBlurService::Native::windowProc;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = name;
		RegisterClassW(&wc);
		return name;
	}();

	return name;
}

} // namespace

LRESULT CALLBACK
WallpaperBlurService::Native::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_SETTINGCHANGE:
		if (wParam == SPI_SETDESKWALLPAPER) WallpaperBlurService::instance()->scheduleUpdate();
		break;
	case WM_DISPLAYCHANGE: WallpaperBlurService::instance()->scheduleUpdate(); break;
	default: break;
	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool WallpaperBlurService::Native::createDevice() {
	this->d3dDevice = nullptr;
	this->d2dDevice = nullptr;

	for (auto driver: {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
		auto hr = D3D11CreateDevice(
		    nullptr,
		    driver,
		    nullptr,
		    D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		    nullptr,
		    0,
		    D3D11_SDK_VERSION,
		    this->d3dDevice.put(),
		    nullptr,
		    nullptr
		);

		if (SUCCEEDED(hr)) break;
	}

	if (!this->d3dDevice) {
		qCWarning(logWallpaperBlur) << "Could not create a Direct3D device.";
		return false;
	}

	auto dxgi = this->d3dDevice.as<IDXGIDevice>();
	if (FAILED(D2D1CreateDevice(dxgi.get(), nullptr, this->d2dDevice.put()))) {
		qCWarning(logWallpaperBlur) << "Could not create a Direct2D device.";
		this->d3dDevice = nullptr;
		return false;
	}

	return true;
}

bool WallpaperBlurService::Native::ensureGraphicsDevice() {
	if (this->graphicsDevice) return true;
	if (!this->createDevice()) return false;

	try {
		auto interop = qs::windows::compositor().as<abi_wuc::ICompositorInterop>();
		winrt::check_hresult(interop->CreateGraphicsDevice(
		    this->d2dDevice.get(),
		    reinterpret_cast<abi_wuc::ICompositionGraphicsDevice**>( // NOLINT
		        winrt::put_abi(this->graphicsDevice)
		    )
		));

		// The surfaces' content is lost with the device.
		this->graphicsDevice.RenderingDeviceReplaced([](auto&&, auto&&) {
			QTimer::singleShot(0, WallpaperBlurService::instance(), [] {
				WallpaperBlurService::instance()->native->drawAll();
			});
		});
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWallpaperBlur) << "Could not create the composition graphics device:"
		                            << QString::fromWCharArray(e.message().c_str());
		this->graphicsDevice = nullptr;
		return false;
	}

	return true;
}

void WallpaperBlurService::Native::draw(Monitor& monitor) {
	if (monitor.blurred.isNull() || !this->ensureGraphicsDevice()) return;

	auto size = winrt::Windows::Foundation::Size {
	    static_cast<float>(monitor.blurred.width()),
	    static_cast<float>(monitor.blurred.height()),
	};

	auto tryDraw = [&]() -> HRESULT {
		if (!monitor.surface) {
			monitor.surface = this->graphicsDevice.CreateDrawingSurface(
			    size,
			    winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
			    winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied
			);
			monitor.source = monitor.surface.as<wuc::ICompositionSurface>();
		} else if (monitor.surface.Size() != size) {
			monitor.surface.Resize({static_cast<int32_t>(size.Width), static_cast<int32_t>(size.Height)});
		}

		auto interop = monitor.surface.as<abi_wuc::ICompositionDrawingSurfaceInterop>();

		winrt::com_ptr<ID2D1DeviceContext> context;
		POINT offset;
		auto hr =
		    interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset);
		if (FAILED(hr)) return hr;

		const auto& image = monitor.blurred;
		winrt::com_ptr<ID2D1Bitmap> bitmap;
		hr = context->CreateBitmap(
		    D2D1::SizeU(image.width(), image.height()),
		    image.constBits(),
		    image.bytesPerLine(),
		    D2D1::BitmapProperties(
		        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
		    ),
		    bitmap.put()
		);

		if (SUCCEEDED(hr)) {
			auto x = static_cast<float>(offset.x);
			auto y = static_cast<float>(offset.y);
			context->DrawBitmap(
			    bitmap.get(),
			    D2D1::RectF(x, y, x + size.Width, y + size.Height),
			    1,
			    D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
			);
		}

		auto end = interop->EndDraw();
		return FAILED(hr) ? hr : end;
	};

	try {
		auto hr = tryDraw();

		// A lost device is replaced once, as the docs advise.
		if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
			if (!this->createDevice()) return;

			auto interop = this->graphicsDevice.as<abi_wuc::ICompositionGraphicsDeviceInterop>();
			winrt::check_hresult(interop->SetRenderingDevice(this->d2dDevice.get()));
			hr = tryDraw();
		}

		winrt::check_hresult(hr);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logWallpaperBlur) << "Could not draw the blurred wallpaper:"
		                            << QString::fromWCharArray(e.message().c_str());
	}
}

void WallpaperBlurService::Native::drawAll() {
	for (auto& monitor: this->monitors) this->draw(monitor);
}

WallpaperBlurService::WallpaperBlurService(): native(std::make_unique<Native>()) {
	auto* n = this->native.get();

	n->hwnd = CreateWindowExW(
	    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
	    windowClass(),
	    L"Quickshell wallpaper blur",
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

	// Wallpaper Engine and slideshows may change the file without a setting change.
	n->poll.setInterval(30000);
	QObject::connect(&n->poll, &QTimer::timeout, this, &WallpaperBlurService::scheduleUpdate);
	n->poll.start();

	this->scheduleUpdate();
}

WallpaperBlurService::~WallpaperBlurService() {
	if (this->native->hwnd) DestroyWindow(this->native->hwnd);
}

WallpaperBlurService* WallpaperBlurService::instance() {
	// Leaked like the compositor: surfaces can't be released after COM shuts down at exit.
	static auto* instance = new WallpaperBlurService(); // NOLINT
	return instance;
}

void WallpaperBlurService::setPasses(qint32 passes) {
	if (passes == this->mPasses) return;
	this->mPasses = passes;
	this->scheduleUpdate();
	emit this->passesChanged();
}

void WallpaperBlurService::setOffset(qreal offset) {
	if (qFuzzyCompare(offset, this->mOffset)) return;
	this->mOffset = offset;
	this->scheduleUpdate();
	emit this->offsetChanged();
}

void WallpaperBlurService::setSaturation(qreal saturation) {
	if (qFuzzyCompare(saturation, this->mSaturation)) return;
	this->mSaturation = saturation;
	this->scheduleUpdate();
	emit this->saturationChanged();
}

void WallpaperBlurService::setNoise(qreal noise) {
	if (qFuzzyCompare(noise, this->mNoise)) return;
	this->mNoise = noise;
	this->scheduleUpdate();
	emit this->noiseChanged();
}

void* WallpaperBlurService::surface(const QRect& monitor) {
	for (const auto& m: this->native->monitors) {
		if (m.wallpaper.rect == monitor && m.source) return winrt::get_abi(m.source);
	}

	return nullptr;
}

void WallpaperBlurService::scheduleUpdate() {
	if (this->updateQueued) return;
	this->updateQueued = true;

	// Batches a config setting every parameter at once.
	QTimer::singleShot(0, this, [this] {
		this->updateQueued = false;
		this->update();
	});
}

void WallpaperBlurService::update() {
	auto* n = this->native.get();

	auto params = BlurParams {
	    .passes = this->mPasses,
	    .offset = this->mOffset,
	    .saturation = this->mSaturation,
	    .noise = this->mNoise,
	};

	auto wallpapers = readWallpapers();

	// Monitors are matched by rect, as backdrops ask for them.
	auto changed = wallpapers.size() != n->monitors.size();
	std::vector<MonitorWallpaper> stale;
	for (size_t i = 0; i != wallpapers.size(); i++) {
		if (changed || n->monitors[i].wallpaper.rect != wallpapers[i].rect) {
			changed = true;
			continue;
		}

		if (n->monitors[i].wallpaper != wallpapers[i] || n->monitors[i].params != params) {
			stale.push_back(wallpapers[i]);
		}
	}

	if (changed) {
		stale.clear();

		// Surfaces of monitors that remain are kept, so backdrops showing them carry on.
		std::vector<Native::Monitor> monitors;
		for (const auto& wallpaper: wallpapers) {
			auto existing = std::ranges::find_if(n->monitors, [&](const auto& m) {
				return m.wallpaper.rect == wallpaper.rect;
			});

			if (existing != n->monitors.end()) {
				if (existing->wallpaper != wallpaper || existing->params != params)
					stale.push_back(wallpaper);
				monitors.push_back(std::move(*existing));
			} else {
				stale.push_back(wallpaper);
				monitors.push_back({.wallpaper = wallpaper});
			}
		}

		n->monitors = std::move(monitors);
		emit this->surfacesChanged();
	}

	if (stale.empty()) return;

	auto generation = ++n->generation;
	qCDebug(logWallpaperBlur) << "Blurring the wallpaper of" << stale.size() << "monitors";

	QThreadPool::globalInstance()->start([this, generation, params, stale = std::move(stale)] {
		std::vector<std::pair<MonitorWallpaper, QImage>> results;
		for (const auto& wallpaper: stale) {
			results.emplace_back(wallpaper, blurWallpaper(drawWallpaper(wallpaper), params));
		}

		QTimer::singleShot(0, this, [this, generation, params, results = std::move(results)] {
			auto* n = this->native.get();
			// A newer blur is underway.
			if (generation != n->generation) return;

			auto added = false;
			for (const auto& [wallpaper, image]: results) {
				for (auto& monitor: n->monitors) {
					if (monitor.wallpaper.rect != wallpaper.rect) continue;

					added |= !monitor.surface;
					monitor.wallpaper = wallpaper;
					monitor.params = params;
					monitor.blurred = image;
					n->draw(monitor);
				}
			}

			if (added) emit this->surfacesChanged();
		});
	});
}

WallpaperBlur::WallpaperBlur(QObject* parent): QObject(parent) {
	auto* service = WallpaperBlurService::instance();
	// clang-format off
	QObject::connect(service, &WallpaperBlurService::passesChanged, this, &WallpaperBlur::passesChanged);
	QObject::connect(service, &WallpaperBlurService::offsetChanged, this, &WallpaperBlur::offsetChanged);
	QObject::connect(service, &WallpaperBlurService::saturationChanged, this, &WallpaperBlur::saturationChanged);
	QObject::connect(service, &WallpaperBlurService::noiseChanged, this, &WallpaperBlur::noiseChanged);
	// clang-format on
}

} // namespace qs::windows
