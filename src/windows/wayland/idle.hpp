#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

// Inert, see layershell.hpp. Quickshell.Windows has working versions of both.
namespace qs::windows::wayland {

class IdleInhibitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(bool enabled MEMBER mEnabled NOTIFY enabledChanged);
	Q_PROPERTY(QObject* window MEMBER mWindow NOTIFY windowChanged);

public:
	explicit IdleInhibitor(QObject* parent = nullptr): QObject(parent) {}

signals:
	void enabledChanged();
	void windowChanged();

private:
	bool mEnabled = false;
	QObject* mWindow = nullptr;
};

class IdleMonitor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	Q_PROPERTY(bool enabled MEMBER mEnabled NOTIFY enabledChanged);
	Q_PROPERTY(qreal timeout MEMBER mTimeout NOTIFY timeoutChanged);
	Q_PROPERTY(bool respectInhibitors MEMBER mRespectInhibitors NOTIFY respectInhibitorsChanged);
	Q_PROPERTY(bool isIdle READ isIdle NOTIFY isIdleChanged);

public:
	explicit IdleMonitor(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static bool isIdle() { return false; }

signals:
	void enabledChanged();
	void timeoutChanged();
	void respectInhibitorsChanged();
	void isIdleChanged();

private:
	bool mEnabled = true;
	qreal mTimeout = 0;
	bool mRespectInhibitors = true;
};

} // namespace qs::windows::wayland
