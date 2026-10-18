#include "background_effect.hpp"

#include <qobject.h>
#include <qqml.h>

#include "../../core/region.hpp"
#include "../blur_behind.hpp"

namespace qs::windows::wayland {

// Never null, see WlrLayershell::qmlAttachedProperties.
BackgroundEffect* BackgroundEffect::qmlAttachedProperties(QObject* object) {
	return new BackgroundEffect(object);
}

void BackgroundEffect::setBlurRegion(PendingRegion* region) {
	if (region == this->mBlurRegion) return;
	this->mBlurRegion = region;

	// Null when not attached to a window.
	auto* blur = qobject_cast<BlurBehind*>(qmlAttachedPropertiesObject<BlurBehind>(this->parent()));
	if (blur) {
		// niri's default for a surface's background effect.
		blur->setXray(true);
		blur->setRegion(region);
	}

	emit this->blurRegionChanged();
}

} // namespace qs::windows::wayland
