#include "platform.hpp"

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qstring.h>
#include <qtypes.h>

#ifdef Q_OS_WIN
#include <atomic>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <io.h>
#include <qcoreapplication.h>
#include <qlogging.h>
#include <qtenvironmentvariables.h>
#include <windows.h>
#include <winioctl.h>
#else
#include <cerrno>
#include <csignal>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace qs::platform {

#ifdef Q_OS_WIN

namespace {

HANDLE fileHandle(QFile* file) {
	// QFile on windows lazily creates a CRT fd for the native handle.
	auto fd = file->handle();
	if (fd == -1) return INVALID_HANDLE_VALUE;
	return reinterpret_cast<HANDLE>(_get_osfhandle(fd)); // NOLINT
}

// Windows locks are mandatory, so locking the file's contents would prevent other processes
// from reading it. Lock a single byte far past the end of the file instead, which is allowed
// and only conflicts with other locks.
OVERLAPPED lockRegion() {
	OVERLAPPED overlapped {};
	overlapped.Offset = 0;
	overlapped.OffsetHigh = 0x7fffffff;
	return overlapped;
}

// REPARSE_DATA_BUFFER lives in the DDK headers (ntifs.h), so the mount point variant is redeclared.
struct MountPointReparseBuffer {
	DWORD reparseTag;
	WORD reparseDataLength;
	WORD reserved;
	WORD substituteNameOffset;
	WORD substituteNameLength;
	WORD printNameOffset;
	WORD printNameLength;
	WCHAR pathBuffer[1];
};

constexpr DWORD REPARSE_HEADER_SIZE = 8; // tag + data length + reserved

} // namespace

int createAnonymousFile(const char* name) {
	static std::atomic<quint32> counter = 0;

	QString fileName = QString::fromUtf8(name).replace(':', '-') % '-'
	              % QString::number(QCoreApplication::applicationPid()) % '-'
	              % QString::number(counter++);
	auto path = QDir::toNativeSeparators(QDir(QDir::tempPath()).filePath(fileName));

	auto* handle = CreateFileW(
	    reinterpret_cast<LPCWSTR>(path.utf16()),
	    GENERIC_READ | GENERIC_WRITE,
	    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
	    nullptr,
	    CREATE_ALWAYS,
	    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
	    nullptr
	);

	if (handle == INVALID_HANDLE_VALUE) return -1;

	auto fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), 0); // NOLINT
	if (fd == -1) CloseHandle(handle);
	return fd;
}

LockResult tryLockExclusive(QFile* file) {
	auto* handle = fileHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return LockResult::Error;

	auto overlapped = lockRegion();
	auto flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
	if (LockFileEx(handle, flags, 0, 1, 0, &overlapped)) return LockResult::Acquired;

	auto error = GetLastError();
	if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) return LockResult::Busy;
	return LockResult::Error;
}

bool waitLockShared(QFile* file) {
	auto* handle = fileHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;

	auto overlapped = lockRegion();
	return LockFileEx(handle, 0, 0, 1, 0, &overlapped);
}

bool isLockedByOther(QFile* file, qint64* /*pid*/) {
	auto* handle = fileHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;

	auto overlapped = lockRegion();
	auto flags = LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
	if (LockFileEx(handle, flags, 0, 1, 0, &overlapped)) {
		overlapped = lockRegion();
		UnlockFileEx(handle, 0, 1, 0, &overlapped);
		return false;
	}

	return GetLastError() == ERROR_LOCK_VIOLATION;
}

bool createDirLink(const QString& target, const QString& link) {
	auto nativeTarget = QDir::toNativeSeparators(QFileInfo(target).absoluteFilePath());
	auto nativeLink = QDir::toNativeSeparators(link);
	QString substituteName = QStringLiteral("\\??\\") % nativeTarget;

	auto substituteBytes = static_cast<WORD>(substituteName.size() * sizeof(WCHAR));
	auto printBytes = static_cast<WORD>(nativeTarget.size() * sizeof(WCHAR));
	auto pathBufferBytes = substituteBytes + sizeof(WCHAR) + printBytes + sizeof(WCHAR);
	auto totalBytes = offsetof(MountPointReparseBuffer, pathBuffer) + pathBufferBytes;

	auto buffer = std::vector<char>(totalBytes, 0);
	auto* reparse = reinterpret_cast<MountPointReparseBuffer*>(buffer.data()); // NOLINT
	reparse->reparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	reparse->reparseDataLength = static_cast<WORD>(totalBytes - REPARSE_HEADER_SIZE);
	reparse->substituteNameOffset = 0;
	reparse->substituteNameLength = substituteBytes;
	reparse->printNameOffset = static_cast<WORD>(substituteBytes + sizeof(WCHAR));
	reparse->printNameLength = printBytes;

	auto* pathBuffer = reinterpret_cast<char*>(reparse->pathBuffer); // NOLINT
	memcpy(pathBuffer, substituteName.utf16(), substituteBytes);
	memcpy(pathBuffer + reparse->printNameOffset, nativeTarget.utf16(), printBytes); // NOLINT

	auto* linkPath = reinterpret_cast<LPCWSTR>(nativeLink.utf16());
	if (!CreateDirectoryW(linkPath, nullptr)) return false;

	auto* handle = CreateFileW(
	    linkPath,
	    GENERIC_WRITE,
	    0,
	    nullptr,
	    OPEN_EXISTING,
	    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
	    nullptr
	);

	if (handle == INVALID_HANDLE_VALUE) {
		auto error = GetLastError();
		RemoveDirectoryW(linkPath);
		SetLastError(error);
		return false;
	}

	DWORD returned = 0;
	auto ok = DeviceIoControl(
	    handle,
	    FSCTL_SET_REPARSE_POINT,
	    buffer.data(),
	    static_cast<DWORD>(totalBytes),
	    nullptr,
	    0,
	    &returned,
	    nullptr
	);

	auto error = GetLastError();
	CloseHandle(handle);

	if (!ok) {
		RemoveDirectoryW(linkPath);
		SetLastError(error);
		return false;
	}

	return true;
}

void removeDirLink(const QString& link) {
	auto nativeLink = QDir::toNativeSeparators(link);
	auto* linkPath = reinterpret_cast<LPCWSTR>(nativeLink.utf16());

	// RemoveDirectoryW removes the junction itself, never the target's contents.
	if (!RemoveDirectoryW(linkPath)) DeleteFileW(linkPath);
}

QString defaultRuntimeDir() { return QDir::tempPath(); }

bool sendSignal(qint64 pid, int signal) {
	// SIGINT, SIGKILL, SIGTERM
	if (signal != 2 && signal != 9 && signal != 15) {
		SetLastError(ERROR_NOT_SUPPORTED);
		return false;
	}

	auto* process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
	if (!process) return false;

	auto ok = TerminateProcess(process, 128 + signal);
	auto error = GetLastError();
	CloseHandle(process);
	SetLastError(error);
	return ok;
}

int spawnDaemon(const char* pipeEnv) {
	SECURITY_ATTRIBUTES attributes {};
	attributes.nLength = sizeof(attributes);
	attributes.bInheritHandle = TRUE;

	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;

	if (!CreatePipe(&readPipe, &writePipe, &attributes, 0)) {
		qFatal().nospace() << "Failed to create messaging pipes for daemon: " << qt_error_string();
	}

	// only the write end should be inherited by the daemon
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	auto envName = QString::fromLatin1(pipeEnv);
	auto envValue = QString::number(reinterpret_cast<quintptr>(writePipe));
	SetEnvironmentVariableW(
	    reinterpret_cast<LPCWSTR>(envName.utf16()),
	    reinterpret_cast<LPCWSTR>(envValue.utf16())
	);

	// Only the status pipe may be inherited. Inheriting the caller's stdio (e.g. a pipe from a
	// shell capturing output) would keep it open for the daemon's lifetime, like the /dev/null
	// redirection does on unix.
	SIZE_T attributeListSize = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListSize);
	auto attributeListBuffer = std::vector<char>(attributeListSize);
	auto* attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>( // NOLINT
	    attributeListBuffer.data()
	);

	if (!InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListSize)
	    || !UpdateProcThreadAttribute(
	        attributeList,
	        0,
	        PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
	        &writePipe,
	        sizeof(writePipe),
	        nullptr,
	        nullptr
	    ))
	{
		qFatal().nospace() << "Failed to set up daemon handle inheritance: " << qt_error_string();
	}

	STARTUPINFOEXW startupInfo {};
	startupInfo.StartupInfo.cb = sizeof(startupInfo);
	startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES; // all null, the daemon has no stdio
	startupInfo.lpAttributeList = attributeList;
	PROCESS_INFORMATION processInfo {};

	// CreateProcessW may modify the command line buffer.
	auto commandLine = std::wstring(GetCommandLineW());

	auto ok = CreateProcessW(
	    nullptr,
	    commandLine.data(),
	    nullptr,
	    nullptr,
	    TRUE,
	    DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP | EXTENDED_STARTUPINFO_PRESENT,
	    nullptr,
	    nullptr,
	    &startupInfo.StartupInfo,
	    &processInfo
	);

	auto error = GetLastError();
	DeleteProcThreadAttributeList(attributeList);
	SetEnvironmentVariableW(reinterpret_cast<LPCWSTR>(envName.utf16()), nullptr);
	CloseHandle(writePipe);

	if (!ok) {
		CloseHandle(readPipe);
		qFatal().nospace() << "Failed to launch daemon: " << qt_error_string(static_cast<int>(error));
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);

	int ret = 0;
	DWORD bytesRead = 0;
	auto readOk = ReadFile(readPipe, &ret, sizeof(ret), &bytesRead, nullptr);
	CloseHandle(readPipe);

	if (!readOk || bytesRead != sizeof(ret)) {
		qFatal() << "Failed to wait for daemon launch (it may have crashed)";
	}

	return ret;
}

int takeDaemonPipe(const char* pipeEnv) {
	auto value = qEnvironmentVariable(pipeEnv);
	if (value.isEmpty()) return -1;
	qunsetenv(pipeEnv);

	bool ok = false;
	auto handle = value.toULongLong(&ok);
	if (!ok) return -1;

	return _open_osfhandle(static_cast<intptr_t>(handle), 0);
}

#else // Q_OS_WIN

int createAnonymousFile(const char* name) { return memfd_create(name, 0); }

LockResult tryLockExclusive(QFile* file) {
	struct flock lock = {
	    .l_type = F_WRLCK,
	    .l_whence = SEEK_SET,
	    .l_start = 0,
	    .l_len = 0,
	    .l_pid = 0,
	};

	if (fcntl(file->handle(), F_SETLK, &lock) == 0) return LockResult::Acquired; // NOLINT
	if (errno == EACCES || errno == EAGAIN) return LockResult::Busy;
	return LockResult::Error;
}

bool waitLockShared(QFile* file) {
	struct flock lock = {
	    .l_type = F_RDLCK, // won't block other read locks when we take it
	    .l_whence = SEEK_SET,
	    .l_start = 0,
	    .l_len = 0,
	    .l_pid = 0,
	};

	return fcntl(file->handle(), F_SETLKW, &lock) == 0; // NOLINT
}

bool isLockedByOther(QFile* file, qint64* pid) {
	struct flock lock = {
	    .l_type = F_WRLCK,
	    .l_whence = SEEK_SET,
	    .l_start = 0,
	    .l_len = 0,
	    .l_pid = 0,
	};

	fcntl(file->handle(), F_GETLK, &lock); // NOLINT
	auto isLocked = lock.l_type != F_UNLCK;
	if (isLocked && pid) *pid = lock.l_pid;
	return isLocked;
}

bool createDirLink(const QString& target, const QString& link) {
	return symlinkat(QFile::encodeName(target).constData(), 0, link.toStdString().c_str()) == 0;
}

void removeDirLink(const QString& link) { QFile::remove(link); }

QString defaultRuntimeDir() { return QString("/run/user/%1").arg(getuid()); }

bool sendSignal(qint64 pid, int signal) { return kill(static_cast<pid_t>(pid), signal) == 0; }

#endif // Q_OS_WIN

} // namespace qs::platform
