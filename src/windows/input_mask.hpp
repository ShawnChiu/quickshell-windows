#pragma once

#include <qregion.h>
#include <qwindow.h>

namespace qs::windows {

// An input mask that leaves what the window draws alone, unlike QWindow::setMask, which on Windows
// is a window region and clips the content too. Outside the mask the window is layered and
// transparent to input while the pointer is there, so clicks reach whatever is below, in any
// process. Installed as ProxyWindowBase::applyInputMask.
void applyInputMask(QWindow* window, const QRegion& mask);

} // namespace qs::windows
