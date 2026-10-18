#pragma once

#include <memory>

#include <qcoreevent.h>
#include <qobject.h>
#include <qpointer.h>
#include <qregion.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qwindow.h>

namespace qs::windows {

// A window of its own, kept just below a Qt window, that blurs what is behind the region with
// Windows.UI.Composition. The Qt window can't host the blur: Qt composes it with
// DirectComposition, which leaves no room for a visual under its content, and the accent policy
// blur (SetWindowCompositionAttribute) always covers the whole window.
//
// The backdrop is a child of the Qt window, so it survives reloads that reuse the window.
class BlurBackdrop: public QObject {
	Q_OBJECT;

public:
	~BlurBackdrop() override;
	Q_DISABLE_COPY_MOVE(BlurBackdrop);

	// Finds or creates the backdrop of the window.
	static BlurBackdrop* forWindow(QWindow* window);

	// The region in device pixels, relative to the window. Empty hides the backdrop.
	void setRegion(const QRegion& region);
	// Standard deviation of the blur in device pixels.
	void setRadius(qreal radius);
	void setSaturation(qreal saturation);

	// Keeps the backdrop until the owner releases it. A reload claims it again before the
	// release takes effect, so the blur stays up across the reload.
	void claim(QObject* owner);
	void release(QObject* owner);

	// Follows the Qt window's position, stacking and visibility.
	void sync();
	void updateEffects();

	bool eventFilter(QObject* watched, QEvent* event) override;

	// Win32 and composition state, see backdrop.cpp.
	struct Native;

private:
	explicit BlurBackdrop(QWindow* window);

	void onSurfaceCreated();
	void onSurfaceDestroyed();
	void ensureCreated();
	void updateShape();
	void updateBrush();

	QWindow* window;
	QPointer<QObject> owner;
	QRegion region;
	qreal radius = 0;
	qreal saturation = 1;

	std::unique_ptr<Native> native;
};

} // namespace qs::windows
