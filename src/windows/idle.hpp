#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/reload.hpp"
#include "../core/util.hpp"

namespace qs::windows {

///! Keeps the display on and the system awake.
/// Uses [SetThreadExecutionState]. Windows tracks this per process, so unlike
/// @@Quickshell.Wayland.IdleInhibitor no window is needed.
///
/// [SetThreadExecutionState]: https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate
class IdleInhibitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	// clang-format off
	/// If the inhibitor should be enabled. Defaults to false.
	Q_PROPERTY(bool enabled READ default WRITE default NOTIFY enabledChanged BINDABLE bindableEnabled);
	// clang-format on

public:
	explicit IdleInhibitor(QObject* parent = nullptr): QObject(parent) {}
	~IdleInhibitor() override;
	Q_DISABLE_COPY_MOVE(IdleInhibitor);

	[[nodiscard]] QBindable<bool> bindableEnabled() { return &this->bEnabled; }

	// True while any inhibitor in this process is enabled.
	static bool anyEnabled();

signals:
	void enabledChanged();

private:
	void updateEnabled();

	bool counted = false;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(IdleInhibitor, bool, bEnabled, &IdleInhibitor::enabledChanged);

	QS_BINDING_SUBSCRIBE_METHOD(IdleInhibitor, bEnabled, updateEnabled, onValueChanged);
	// clang-format on
};

///! Reports when the user stops providing input.
/// @@isIdle is set after @@timeout seconds without keyboard or mouse input, found by polling
/// [GetLastInputInfo].
///
/// [GetLastInputInfo]: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getlastinputinfo
class IdleMonitor: public PostReloadHook {
	Q_OBJECT;
	QML_ELEMENT;
	// clang-format off
	/// If the idle monitor should be enabled. Defaults to true.
	Q_PROPERTY(bool enabled READ default WRITE default NOTIFY enabledChanged BINDABLE bindableEnabled);
	/// Seconds without input before @@isIdle is set. Defaults to 0.
	Q_PROPERTY(qreal timeout READ default WRITE default NOTIFY timeoutChanged BINDABLE bindableTimeout);
	/// If an enabled @@IdleInhibitor keeps @@isIdle false. Only inhibitors in this process are
	/// seen, Windows does not expose other programs' requests. Defaults to true.
	Q_PROPERTY(bool respectInhibitors READ default WRITE default NOTIFY respectInhibitorsChanged BINDABLE bindableRespectInhibitors);
	Q_PROPERTY(bool isIdle READ default NOTIFY isIdleChanged BINDABLE bindableIsIdle);
	// clang-format on

public:
	explicit IdleMonitor(QObject* parent = nullptr);

	void onPostReload() override;

	[[nodiscard]] QBindable<bool> bindableEnabled() { return &this->bEnabled; }
	[[nodiscard]] QBindable<qreal> bindableTimeout() { return &this->bTimeout; }
	[[nodiscard]] QBindable<bool> bindableRespectInhibitors() { return &this->bRespectInhibitors; }
	[[nodiscard]] QBindable<bool> bindableIsIdle() const { return &this->bIsIdle; }

signals:
	void enabledChanged();
	void timeoutChanged();
	void respectInhibitorsChanged();
	void isIdleChanged();

private slots:
	void poll();

private:
	QTimer timer;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(IdleMonitor, bool, bEnabled, true, &IdleMonitor::enabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(IdleMonitor, qreal, bTimeout, &IdleMonitor::timeoutChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(IdleMonitor, bool, bRespectInhibitors, true, &IdleMonitor::respectInhibitorsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(IdleMonitor, bool, bIsIdle, &IdleMonitor::isIdleChanged);

	QS_BINDING_SUBSCRIBE_METHOD(IdleMonitor, bEnabled, poll, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(IdleMonitor, bTimeout, poll, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(IdleMonitor, bRespectInhibitors, poll, onValueChanged);
	// clang-format on
};

} // namespace qs::windows
