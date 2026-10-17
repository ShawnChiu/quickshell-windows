#include "idle.hpp"

#include <limits>

#include <qobject.h>
#include <qtypes.h>

#include <windows.h>

#include "../core/reload.hpp"

namespace qs::windows {

namespace {

qint32 enabledInhibitors = 0; // NOLINT

// Input can't be waited on, so an idle monitor polls at this rate to notice it.
constexpr qint32 IDLE_POLL_MS = 500;

void updateExecutionState() {
	SetThreadExecutionState(
	    enabledInhibitors > 0 ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED
	                          : ES_CONTINUOUS
	);
}

} // namespace

IdleInhibitor::~IdleInhibitor() {
	if (!this->counted) return;
	enabledInhibitors--;
	updateExecutionState();
}

bool IdleInhibitor::anyEnabled() { return enabledInhibitors > 0; }

void IdleInhibitor::updateEnabled() {
	auto enabled = this->bEnabled.value();
	if (enabled == this->counted) return;

	this->counted = enabled;
	enabledInhibitors += enabled ? 1 : -1;
	updateExecutionState();
}

IdleMonitor::IdleMonitor(QObject* parent): PostReloadHook(parent) {
	this->timer.setSingleShot(true);
	QObject::connect(&this->timer, &QTimer::timeout, this, &IdleMonitor::poll);
}

void IdleMonitor::onPostReload() { this->poll(); }

void IdleMonitor::poll() {
	if (!this->isPostReload) return;

	if (!this->bEnabled) {
		this->timer.stop();
		this->bIsIdle = false;
		return;
	}

	LASTINPUTINFO info {};
	info.cbSize = sizeof(info);
	GetLastInputInfo(&info);

	// Both are GetTickCount values, unsigned subtraction handles wraparound.
	auto sinceInput = static_cast<qint64>(GetTickCount() - info.dwTime);
	auto timeout = qMax(0ll, qRound64(this->bTimeout * 1000));
	auto inhibited = this->bRespectInhibitors && IdleInhibitor::anyEnabled();
	auto idle = !inhibited && sinceInput >= timeout;

	this->bIsIdle = idle;

	auto next = idle || inhibited ? IDLE_POLL_MS : timeout - sinceInput;
	this->timer.start(static_cast<int>(qMin<qint64>(next, std::numeric_limits<int>::max())));
}

} // namespace qs::windows
