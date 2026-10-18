#pragma once

#include <qcoreevent.h>
#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qquickwindow.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/region.hpp"
#include "../window/proxywindow.hpp"

namespace qs::windows {

///! Blur behind a window.
/// Blurs what is behind @@region of a @@Quickshell.QsWindow or subclass, as an attached
/// object. Unlike Mica and Acrylic the blur stays while the window is unfocused.
///
/// Like Windows' own blur it falls back to a solid colour while "Transparency effects"
/// is off or battery or energy saver is on.
///
/// The defaults follow Acrylic's recipe.
///
/// #### Example
/// ```qml
/// @@Quickshell.PanelWindow {
///   id: root
///   color: "#80000000"
///
///   BlurBehind.region: Region { item: root.contentItem }
/// }
/// ```
class BlurBehind: public QObject {
	Q_OBJECT;
	// clang-format off
	/// Region to blur behind the window. Set to null to remove blur.
	Q_PROPERTY(PendingRegion* region READ region WRITE setRegion NOTIFY regionChanged);
	/// Standard deviation of the blur in logical pixels. Defaults to 30.
	Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged);
	/// Saturation applied after the blur, 1 leaves colours as they are. Defaults to 1.25.
	Q_PROPERTY(qreal saturation READ saturation WRITE setSaturation NOTIFY saturationChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("BlurBehind can only be used as an attached object.");
	QML_ATTACHED(BlurBehind);

public:
	explicit BlurBehind(ProxyWindowBase* window);
	~BlurBehind() override;
	Q_DISABLE_COPY_MOVE(BlurBehind);

	[[nodiscard]] PendingRegion* region() const { return this->mRegion; }
	void setRegion(PendingRegion* region);

	[[nodiscard]] qreal radius() const { return this->mRadius; }
	void setRadius(qreal radius);

	[[nodiscard]] qreal saturation() const { return this->mSaturation; }
	void setSaturation(qreal saturation);

	static BlurBehind* qmlAttachedProperties(QObject* object);

signals:
	void regionChanged();
	void radiusChanged();
	void saturationChanged();

private slots:
	void onWindowConnected();
	void onProxyWindowDestroyed();
	void onRegionDestroyed();
	void onWindowPolished();
	void updateRegion();

private:
	ProxyWindowBase* proxyWindow = nullptr;
	QPointer<QQuickWindow> mWindow;

	bool pendingRegion = false;
	PendingRegion* mRegion = nullptr;
	qreal mRadius = 30;
	qreal mSaturation = 1.25;
};

} // namespace qs::windows
