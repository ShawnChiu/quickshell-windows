#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../core/region.hpp"

namespace qs::windows::wayland {

// Forwards blurRegion to Quickshell.Windows.BlurBehind, so shared configs blur on Windows too.
class BackgroundEffect: public QObject {
	Q_OBJECT;
	Q_PROPERTY(PendingRegion* blurRegion READ blurRegion WRITE setBlurRegion NOTIFY blurRegionChanged);
	QML_ELEMENT;
	QML_UNCREATABLE("BackgroundEffect can only be used as an attached object.");
	QML_ATTACHED(BackgroundEffect);

public:
	explicit BackgroundEffect(QObject* parent): QObject(parent) {}

	[[nodiscard]] PendingRegion* blurRegion() const { return this->mBlurRegion; }
	void setBlurRegion(PendingRegion* region);

	static BackgroundEffect* qmlAttachedProperties(QObject* object);

signals:
	void blurRegionChanged();

private:
	QPointer<PendingRegion> mBlurRegion;
};

} // namespace qs::windows::wayland
