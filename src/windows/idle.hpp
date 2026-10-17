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

// Keeps the display on and the system awake while enabled, using SetThreadExecutionState.
// Windows tracks this per process, so no window is needed.
class IdleInhibitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	// clang-format off
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

// Reports when there has been no keyboard or mouse input for timeout seconds, by polling
// GetLastInputInfo.
class IdleMonitor: public PostReloadHook {
	Q_OBJECT;
	QML_ELEMENT;
	// clang-format off
	// Defaults to true.
	Q_PROPERTY(bool enabled READ default WRITE default NOTIFY enabledChanged BINDABLE bindableEnabled);
	// Seconds without input before isIdle is set.
	Q_PROPERTY(qreal timeout READ default WRITE default NOTIFY timeoutChanged BINDABLE bindableTimeout);
	// If an enabled IdleInhibitor keeps isIdle false. Only inhibitors in this process are
	// seen, Windows does not expose other programs' requests. Defaults to true.
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
