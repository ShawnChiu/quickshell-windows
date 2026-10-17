#pragma once

#include <qfile.h>
#include <qstring.h>
#include <qtypes.h>

// Thin wrappers over the OS primitives quickshell's core needs (anonymous files,
// advisory locks, directory links, signals). The Linux implementations keep the
// original POSIX behavior; Windows implementations use Win32 equivalents.
namespace qs::platform {

/// Creates an anonymous read/write file which is removed once closed.
/// Returns a file descriptor usable with QFile::open(int, ...), or -1 on failure.
int createAnonymousFile(const char* name);

enum class LockResult : quint8 {
	Acquired,
	Busy,
	Error,
};

/// Takes a non-blocking exclusive lock marking the file as owned by this process.
/// The lock is held until the file is closed.
LockResult tryLockExclusive(QFile* file);

/// Blocks until a shared lock can be taken, i.e. until no exclusive lock is held.
bool waitLockShared(QFile* file);

/// Checks if another process holds an exclusive lock on the file.
/// If the platform can report the holder's pid it is written to `pid`, otherwise `pid` is untouched.
bool isLockedByOther(QFile* file, qint64* pid);

/// Creates a link at `link` pointing to the directory `target`.
/// Symlink on unix, NTFS junction on Windows (junctions need no extra privileges).
bool createDirLink(const QString& target, const QString& link);

/// Removes a link created by createDirLink without touching its target.
void removeDirLink(const QString& link);

/// The base directory for runtime files such as sockets and lock files,
/// used when XDG_RUNTIME_DIR is not set.
QString defaultRuntimeDir();

/// Sends a POSIX style signal to a process. On Windows only termination signals
/// (SIGINT, SIGKILL, SIGTERM) are supported, and all of them terminate the process.
bool sendSignal(qint64 pid, int signal);

#ifdef Q_OS_WIN
/// Windows has no fork(), so daemonizing relaunches the current command line as a process
/// detached from the console. The child receives the write end of a pipe through the
/// `pipeEnv` environment variable (see takeDaemonPipe) and reports its startup status into it.
/// Returns the status written by the child.
int spawnDaemon(const char* pipeEnv);

/// If this process was started by spawnDaemon, returns a file descriptor for the status pipe
/// and clears `pipeEnv`. Returns -1 otherwise.
int takeDaemonPipe(const char* pipeEnv);
#endif

} // namespace qs::platform
