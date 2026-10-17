#include "layershell.hpp"

#include <qobject.h>

namespace qs::windows::wayland {

// Unlike X11 this never returns null, as a null attached object makes the engine silently
// stop creating the rest of the component.
WlrLayershell* WlrLayershell::qmlAttachedProperties(QObject* object) {
	return new WlrLayershell(object);
}

} // namespace qs::windows::wayland
