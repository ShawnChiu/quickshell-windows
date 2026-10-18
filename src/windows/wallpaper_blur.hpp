#pragma once

#include <memory>

#include <qobject.h>
#include <qqmlintegration.h>
#include <qrect.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows {

// The desktop wallpaper of each monitor, blurred once with niri's blur and kept as a composition
// surface that every xray backdrop on the monitor shows its part of. Process wide, so the blur
// survives reloads.
class WallpaperBlurService: public QObject {
	Q_OBJECT;

public:
	~WallpaperBlurService() override;
	Q_DISABLE_COPY_MOVE(WallpaperBlurService);

	static WallpaperBlurService* instance();

	[[nodiscard]] qint32 passes() const { return this->mPasses; }
	void setPasses(qint32 passes);
	[[nodiscard]] qreal offset() const { return this->mOffset; }
	void setOffset(qreal offset);
	[[nodiscard]] qreal saturation() const { return this->mSaturation; }
	void setSaturation(qreal saturation);
	[[nodiscard]] qreal noise() const { return this->mNoise; }
	void setNoise(qreal noise);

	// The blurred wallpaper of the monitor whose rect, in physical pixels, is `monitor`, as a
	// winrt::Windows::UI::Composition::ICompositionSurface ABI pointer. Null until the first blur
	// of it is done. The pointer stays valid while the monitor does.
	void* surface(const QRect& monitor);

	// Win32 and composition state, see wallpaper_blur.cpp.
	struct Native;

signals:
	void passesChanged();
	void offsetChanged();
	void saturationChanged();
	void noiseChanged();
	// Monitors were added or removed, so backdrops may need another surface.
	void surfacesChanged();

private:
	explicit WallpaperBlurService();

	void scheduleUpdate();
	void update();

	qint32 mPasses = 3;
	qreal mOffset = 3;
	qreal mSaturation = 1.5;
	qreal mNoise = 0.02;

	bool updateQueued = false;
	std::unique_ptr<Native> native;
};

///! Parameters of the blurred wallpaper xray windows show.
/// Windows with @@BlurBehind.xray set show the desktop wallpaper blurred once with these
/// parameters, as niri's `blur` section does for its xray surfaces. The defaults are niri's.
///
/// The values are kept across reloads.
class WallpaperBlur: public QObject {
	Q_OBJECT;
	// clang-format off
	/// Downsampling passes of the dual Kawase blur. Each doubles its reach. Defaults to 3.
	Q_PROPERTY(qint32 passes READ passes WRITE setPasses NOTIFY passesChanged);
	/// Sample offset of each pass, in device pixels. Defaults to 3.
	Q_PROPERTY(qreal offset READ offset WRITE setOffset NOTIFY offsetChanged);
	/// Saturation applied after the blur, 1 leaves colours as they are. Defaults to 1.5.
	Q_PROPERTY(qreal saturation READ saturation WRITE setSaturation NOTIFY saturationChanged);
	/// Amount of noise added against banding. Defaults to 0.02.
	Q_PROPERTY(qreal noise READ noise WRITE setNoise NOTIFY noiseChanged);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit WallpaperBlur(QObject* parent = nullptr);

	[[nodiscard]] static qint32 passes() { return WallpaperBlurService::instance()->passes(); }
	static void setPasses(qint32 passes) { WallpaperBlurService::instance()->setPasses(passes); }
	[[nodiscard]] static qreal offset() { return WallpaperBlurService::instance()->offset(); }
	static void setOffset(qreal offset) { WallpaperBlurService::instance()->setOffset(offset); }
	[[nodiscard]] static qreal saturation() { return WallpaperBlurService::instance()->saturation(); }
	static void setSaturation(qreal saturation) {
		WallpaperBlurService::instance()->setSaturation(saturation);
	}
	[[nodiscard]] static qreal noise() { return WallpaperBlurService::instance()->noise(); }
	static void setNoise(qreal noise) { WallpaperBlurService::instance()->setNoise(noise); }

signals:
	void passesChanged();
	void offsetChanged();
	void saturationChanged();
	void noiseChanged();
};

} // namespace qs::windows
