#pragma once

// C++/WinRT comes before Qt, whose keyword macros it doesn't expect: include this first.
#include <winrt/Windows.UI.Composition.h>

namespace qs::windows {

// The process' compositor, on a DispatcherQueue that Qt's event loop pumps. Defined in backdrop.cpp.
winrt::Windows::UI::Composition::Compositor compositor();

} // namespace qs::windows
