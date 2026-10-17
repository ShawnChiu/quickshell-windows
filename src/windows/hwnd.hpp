#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

namespace qs::windows {

///! Actions on top level windows by handle.
/// Takes window handles (HWNDs) as numbers, as window managers such as komorebi report them.
class HwndActions: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Hwnd);
	QML_SINGLETON;

public:
	explicit HwndActions(QObject* parent = nullptr): QObject(parent) {}

	/// Restores the window if minimized and brings it to the foreground.
	/// Returns false if Windows refused, which it does unless this process received
	/// the last input event (a click on one of its windows does).
	Q_INVOKABLE static bool activate(qint64 hwnd);
	/// Asks the window to close (WM_CLOSE). It may refuse or prompt first.
	Q_INVOKABLE static bool close(qint64 hwnd);
	/// The id of the process that owns the window, or 0 if there is no such window.
	Q_INVOKABLE static qint32 processId(qint64 hwnd);
};

} // namespace qs::windows
