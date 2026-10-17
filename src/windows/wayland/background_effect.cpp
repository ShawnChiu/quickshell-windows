#include "background_effect.hpp"

#include <qobject.h>

#include "../../core/region.hpp"

namespace qs::windows::wayland {

// Never null, see WlrLayershell::qmlAttachedProperties.
BackgroundEffect* BackgroundEffect::qmlAttachedProperties(QObject* object) {
	return new BackgroundEffect(object);
}

void BackgroundEffect::setBlurRegion(PendingRegion* region) {
	if (region == this->mBlurRegion) return;
	this->mBlurRegion = region;
	emit this->blurRegionChanged();
}

} // namespace qs::windows::wayland
