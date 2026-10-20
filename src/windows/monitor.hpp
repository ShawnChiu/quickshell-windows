#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../core/qmlscreen.hpp"

namespace qs::windows {

///! Information about displays (monitors) that Qt does not report.
/// Window managers name displays by their GDI device name (`\\.\DISPLAY1`), while
/// @@Quickshell.ShellScreen.name is the display's model name. @@deviceName() matches the two.
class MonitorInfo: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Monitor);
	QML_SINGLETON;

public:
	explicit MonitorInfo(QObject* parent = nullptr): QObject(parent) {}

	/// The GDI device name of the screen, such as `\\.\DISPLAY1`, or an empty string
	/// if the screen is no longer connected.
	Q_INVOKABLE static QString deviceName(QuickshellScreenInfo* screen);
};

} // namespace qs::windows
