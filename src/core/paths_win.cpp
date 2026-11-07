#include <qdatetime.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qset.h>
#include <qstring.h>
#include <qtimezone.h>
#include <qtypes.h>
#include <windows.h>
#include <wtsapi32.h>

#include "logcat.hpp"
#include "paths.hpp"
#include "platform.hpp"

namespace {
QS_LOGGING_CATEGORY(logPaths, "quickshell.paths", QtWarningMsg);

QDateTime sessionLogonTime() {
	WTSINFOW* info = nullptr;
	DWORD size = 0;

	if (!WTSQuerySessionInformationW(
	        WTS_CURRENT_SERVER_HANDLE,
	        WTS_CURRENT_SESSION,
	        WTSSessionInfo,
	        reinterpret_cast<LPWSTR*>(&info), // NOLINT
	        &size
	    ))
	{
		return {};
	}

	auto fileTime = info->LogonTime.QuadPart;
	WTSFreeMemory(info);
	if (fileTime == 0) return {};

	// FILETIME counts 100ns intervals from 1601.
	constexpr qint64 epochOffset = 116444736000000000;
	return QDateTime::fromMSecsSinceEpoch((fileTime - epochOffset) / 10000, QTimeZone::UTC);
}

} // namespace

void QsPaths::cleanRunDir() {
	auto* runDir = this->baseRunDir();
	if (!runDir) return;

	auto logonTime = sessionLogonTime();
	if (!logonTime.isValid()) return;

	auto byIdDir = QDir(runDir->filePath("by-id"));
	auto staleIds = QSet<QString>();
	InstanceLockInfo info;

	for (auto& id: byIdDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		auto path = byIdDir.filePath(id);
		QDateTime launchTime;

		if (QsPaths::checkLock(path, &info, true)) {
			if (info.pid != -1) continue;
			launchTime = info.instance.launchTime;
		}

		if (!launchTime.isValid()) launchTime = QFileInfo(path).birthTime();
		if (launchTime.isValid() && launchTime < logonTime) staleIds.insert(id);
	}

	if (staleIds.isEmpty()) return;

	// Links first, while their targets still exist.
	auto removeLinks = [&](const QDir& dir) {
		for (auto& entry: dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			if (!entry.isJunction()) continue;
			if (!staleIds.contains(QFileInfo(entry.junctionTarget()).fileName())) continue;
			qs::platform::removeDirLink(entry.filePath());
		}
	};

	removeLinks(QDir(runDir->filePath("by-pid")));

	auto byShellDir = QDir(runDir->filePath("by-shell"));
	for (auto& shell: byShellDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		removeLinks(QDir(byShellDir.filePath(shell)));
	}

	for (const auto& id: staleIds) {
		if (QDir(byIdDir.filePath(id)).removeRecursively()) {
			qCDebug(logPaths) << "Removed instance runtime path from an earlier session:" << id;
		}
	}
}
