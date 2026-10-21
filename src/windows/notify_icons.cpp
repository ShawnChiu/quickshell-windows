#include "notify_icons.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <thread>
#include <vector>

// The other Windows headers need it first.
// clang-format off
#include <windows.h>
// clang-format on

#include <qdebug.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qstring.h>
#include <qtypes.h>
#include <quuid.h>
#include <shellapi.h>
#include <shlwapi.h>

#include "../core/logcat.hpp"

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logNotifyIcons, "quickshell.windows.notifyicons", QtWarningMsg);

// NOTIFYICONDATAW as Shell_NotifyIcon sends it, with 32 bit handles so 32 and 64 bit programs agree.
struct NotifyIconData32 {
	DWORD cbSize;
	DWORD hWnd;
	UINT uID;
	UINT uFlags;
	UINT uCallbackMessage;
	DWORD hIcon;
	WCHAR szTip[128];
	DWORD dwState;
	DWORD dwStateMask;
	WCHAR szInfo[256];
	UINT uVersion; // or uTimeout
	WCHAR szInfoTitle[64];
	DWORD dwInfoFlags;
	GUID guidItem;
	DWORD hBalloonIcon;
};

struct TrayData {
	DWORD signature;
	DWORD message;
	NotifyIconData32 nid;
};

// APPBARDATA as SHAppBarMessage sends it, the same for 32 and 64 bit programs.
struct AppBarData32 {
	DWORD cbSize;
	DWORD hWnd;
	UINT uCallbackMessage;
	UINT uEdge;
	RECT rc;
	qint64 lParam;
};

struct AppBarMessage {
	AppBarData32 abd;
	DWORD message;
	quint64 sharedMemory; // AppBarData32 in sourceProcessId, for replies
	DWORD sourceProcessId;
};

static_assert(sizeof(AppBarMessage) == 64);

constexpr DWORD TRAY_DATA_SIGNATURE = 0x34753423;
constexpr ULONG_PTR COPYDATA_APPBAR = 0;
constexpr ULONG_PTR COPYDATA_NOTIFY_ICON = 1;
constexpr UINT_PTR TIMER_ID = 1;
constexpr UINT TIMER_MS = 100;
constexpr UINT FORWARD_TIMEOUT_MS = 5000;
constexpr ULONGLONG EXPLORER_START_MS = 10000;

bool isExplorer(HWND hwnd) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);

	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return false;

	wchar_t path[MAX_PATH];
	auto size = static_cast<DWORD>(std::size(path));
	auto ok = QueryFullProcessImageNameW(process, 0, path, &size);
	CloseHandle(process);

	return ok && QString::fromWCharArray(path, size).endsWith(u"\\explorer.exe", Qt::CaseInsensitive);
}

} // namespace

QDebug operator<<(QDebug debug, const NotifyIconData& icon) {
	auto saver = QDebugStateSaver(debug);
	debug.nospace() << "NotifyIcon(pid=" << icon.processId << ", id=" << icon.id;
	if (!icon.guid.isNull()) debug << ", guid=" << icon.guid;
	debug << ", tooltip=" << icon.tooltip << ", icon=" << icon.icon.size()
	      << ", hidden=" << icon.hidden << ", version=" << icon.version << ')';
	return debug;
}

// Lives on its own thread, since every Shell_NotifyIcon and SHAppBarMessage call blocks on it.
class NotifyIconServicePrivate {
public:
	explicit NotifyIconServicePrivate(NotifyIconService* service): service(service) {}

	void run();

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT handleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	HWND findExplorer();
	LRESULT forward(UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT forwardAppBar(WPARAM wParam, const COPYDATASTRUCT* data);
	void checkPosition();
	void takeOver();
	void yieldToExplorer();
	void raise();
	void broadcastTaskbarCreated();
	void handleTrayData(const COPYDATASTRUCT* data);
	void removeStale();
	void postUpdate(const NotifyIconData& icon);
	void postRemove(quint64 key);

	NotifyIconService* service;
	HWND hwnd = nullptr;
	HWND explorer = nullptr;
	UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	bool yielded = false;
	ULONGLONG explorerFoundAt = 0;
	quint64 nextKey = 1;
	std::vector<NotifyIconData> icons;
};

void NotifyIconServicePrivate::run() {
	auto* instance = GetModuleHandleW(nullptr);

	auto wc = WNDCLASSW {};
	wc.lpfnWndProc = &NotifyIconServicePrivate::windowProc;
	wc.hInstance = instance;
	wc.lpszClassName = L"Shell_TrayWnd";
	RegisterClassW(&wc);

	this->hwnd = CreateWindowExW(
	    WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
	    L"Shell_TrayWnd",
	    nullptr,
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    instance,
	    this
	);

	if (!this->hwnd) {
		qCWarning(logNotifyIcons) << "Could not create the tray window:" << GetLastError();
		return;
	}

	// Without these, UIPI drops them from non-elevated programs while running as administrator.
	ChangeWindowMessageFilterEx(this->hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
	ChangeWindowMessageFilterEx(this->hwnd, this->taskbarCreated, MSGFLT_ALLOW, nullptr);

	SetTimer(this->hwnd, TIMER_ID, TIMER_MS, nullptr);
	if (this->findExplorer()) this->takeOver();
	else this->yieldToExplorer();

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
		DispatchMessageW(&msg);
	}
}

LRESULT CALLBACK
NotifyIconServicePrivate::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_NCCREATE) {
		const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam); // NOLINT
		auto userData = reinterpret_cast<LONG_PTR>(create->lpCreateParams);  // NOLINT
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, userData);
	}

	auto* self =
	    reinterpret_cast<NotifyIconServicePrivate*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); // NOLINT
	if (!self || !self->hwnd) return DefWindowProcW(hwnd, msg, wParam, lParam);

	return self->handleMessage(msg, wParam, lParam);
}

LRESULT NotifyIconServicePrivate::handleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == this->taskbarCreated) {
		// Explorer's tray is ready again after a restart. Other tray interceptors broadcast this too,
		// and taking over from them would broadcast back forever.
		auto* previous = this->explorer;
		this->explorer = nullptr;
		if (this->findExplorer() && (this->yielded || this->explorer != previous)) this->takeOver();
		return 0;
	}

	switch (msg) {
	case WM_COPYDATA: {
		const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam); // NOLINT
		if (data && data->dwData == COPYDATA_APPBAR) return this->forwardAppBar(wParam, data);
		if (data && data->dwData == COPYDATA_NOTIFY_ICON) this->handleTrayData(data);
		return this->forward(msg, wParam, lParam);
	}
	case WM_TIMER:
		if (wParam != TIMER_ID) break;
		this->checkPosition();
		this->removeStale();
		return 0;
	// Only sent by programs looking for the taskbar, which would close ours or do nothing.
	case WM_CLOSE:
	case WM_COMMAND:
	case WM_SYSCOMMAND: return this->forward(msg, wParam, lParam);
	case WM_DESTROY: PostQuitMessage(0); return 0;
	default: break;
	}

	if (msg >= WM_USER) return this->forward(msg, wParam, lParam);
	return DefWindowProcW(this->hwnd, msg, wParam, lParam);
}

HWND NotifyIconServicePrivate::findExplorer() {
	if (this->explorer && IsWindow(this->explorer)) return this->explorer;
	this->explorer = nullptr;

	// Other programs intercepting the tray may forward to ours, so only Explorer's will do.
	HWND window = nullptr;
	while ((window = FindWindowExW(nullptr, window, L"Shell_TrayWnd", nullptr))) {
		if (window != this->hwnd && isExplorer(window)) {
			this->explorer = window;
			break;
		}
	}

	return this->explorer;
}

LRESULT NotifyIconServicePrivate::forward(UINT msg, WPARAM wParam, LPARAM lParam) {
	auto* explorer = this->findExplorer();
	if (!explorer) return 0;

	if (msg != WM_COPYDATA && (InSendMessageEx(nullptr) & ISMEX_SEND) == 0) {
		PostMessageW(explorer, msg, wParam, lParam);
		return 0;
	}

	DWORD_PTR result = 0;
	if (!SendMessageTimeoutW(
	        explorer,
	        msg,
	        wParam,
	        lParam,
	        SMTO_ABORTIFHUNG,
	        FORWARD_TIMEOUT_MS,
	        &result
	    ))
	{
		qCDebug(logNotifyIcons) << "Could not forward message" << Qt::hex << msg
		                        << "to Explorer:" << GetLastError();
	}

	return static_cast<LRESULT>(result);
}

LRESULT NotifyIconServicePrivate::forwardAppBar(WPARAM wParam, const COPYDATASTRUCT* data) {
	auto message = AppBarMessage {};
	if (!data->lpData || data->cbData != sizeof(message)) {
		return this->forward(WM_COPYDATA, wParam, reinterpret_cast<LPARAM>(data)); // NOLINT
	}

	std::memcpy(&message, data->lpData, sizeof(message));
	if (message.sharedMemory == 0) {
		return this->forward(WM_COPYDATA, wParam, reinterpret_cast<LPARAM>(data)); // NOLINT
	}

	auto* explorer = this->findExplorer();
	if (!explorer) return 0;

	DWORD explorerPid = 0;
	GetWindowThreadProcessId(explorer, &explorerPid);

	// SHAppBarMessage allocates the reply for our process, which Explorer can't open while we run as
	// administrator, and passes an in-process block when the caller is this process.
	auto* source = SHLockShared(
	    reinterpret_cast<HANDLE>(message.sharedMemory),
	    message.sourceProcessId
	); // NOLINT

	// Fails for callers running as administrator while we don't, which then get no reply.
	auto* copy = SHAllocShared(source ? source : &message.abd, sizeof(AppBarData32), explorerPid);
	if (!copy) {
		if (source) SHUnlockShared(source);
		return this->forward(WM_COPYDATA, wParam, reinterpret_cast<LPARAM>(data)); // NOLINT
	}

	message.sharedMemory = reinterpret_cast<quintptr>(copy);
	message.sourceProcessId = explorerPid;

	auto forwarded = COPYDATASTRUCT {COPYDATA_APPBAR, sizeof(message), &message};
	auto result = this->forward(WM_COPYDATA, wParam, reinterpret_cast<LPARAM>(&forwarded)); // NOLINT

	if (source) {
		if (auto* reply = SHLockShared(copy, explorerPid)) {
			std::memcpy(source, reply, sizeof(AppBarData32));
			SHUnlockShared(reply);
		}

		SHUnlockShared(source);
	}

	SHFreeShared(copy, explorerPid);
	return result;
}

void NotifyIconServicePrivate::checkPosition() {
	if (!this->findExplorer()) {
		if (!this->yielded) this->yieldToExplorer();
		this->explorerFoundAt = 0;
		return;
	}

	if (this->yielded) {
		// In case its TaskbarCreated never reaches us.
		if (this->explorerFoundAt == 0) this->explorerFoundAt = GetTickCount64();
		if (GetTickCount64() - this->explorerFoundAt >= EXPLORER_START_MS) this->takeOver();
		return;
	}

	if (FindWindowW(L"Shell_TrayWnd", nullptr) != this->hwnd) this->raise();
}

void NotifyIconServicePrivate::takeOver() {
	qCDebug(logNotifyIcons) << "Taking over the tray window from Explorer";
	this->yielded = false;
	this->raise();
	this->broadcastTaskbarCreated();
}

// Explorer messages its own tray window while starting, which fails if it finds ours and we run
// as administrator, so stay behind it until it's ready.
void NotifyIconServicePrivate::yieldToExplorer() {
	qCDebug(logNotifyIcons) << "Waiting for Explorer's tray";
	this->yielded = true;
	SetWindowPos(this->hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

void NotifyIconServicePrivate::raise() {
	auto rect = RECT {};
	UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER;

	// Some programs find the taskbar's edge from the tray window's rect.
	if (auto* explorer = this->findExplorer()) GetWindowRect(explorer, &rect);
	else flags |= SWP_NOMOVE | SWP_NOSIZE;

	SetWindowPos(
	    this->hwnd,
	    HWND_TOPMOST,
	    rect.left,
	    rect.top,
	    rect.right - rect.left,
	    rect.bottom - rect.top,
	    flags
	);
}

void NotifyIconServicePrivate::broadcastTaskbarCreated() {
	SendNotifyMessageW(HWND_BROADCAST, this->taskbarCreated, 0, 0);
}

void NotifyIconServicePrivate::handleTrayData(const COPYDATASTRUCT* data) {
	if (!data->lpData || data->cbData < offsetof(TrayData, nid) + offsetof(NotifyIconData32, szTip))
		return;

	// Older programs send a shorter NOTIFYICONDATAW.
	auto tray = TrayData {};
	std::memcpy(&tray, data->lpData, std::min<size_t>(data->cbData, sizeof(tray)));
	if (tray.signature != TRAY_DATA_SIGNATURE) return;

	const auto& nid = tray.nid;
	auto* owner = static_cast<HWND>(LongToHandle(static_cast<LONG>(nid.hWnd)));
	auto guid = (nid.uFlags & NIF_GUID) ? QUuid(nid.guidItem) : QUuid();

	auto icon = std::ranges::find_if(this->icons, [&](const NotifyIconData& icon) {
		return guid.isNull() ? icon.hwnd == reinterpret_cast<quintptr>(owner) && icon.id == nid.uID
		                     : icon.guid == guid;
	});

	switch (tray.message) {
	case NIM_ADD:
		// A GUID icon added again from another window, such as after its program restarted.
		if (icon != this->icons.end() && icon->hwnd != reinterpret_cast<quintptr>(owner)) {
			this->postRemove(icon->key);
			this->icons.erase(icon);
			icon = this->icons.end();
		}

		if (icon == this->icons.end()) {
			auto added = NotifyIconData();
			added.key = this->nextKey++;
			added.hwnd = reinterpret_cast<quintptr>(owner);
			added.id = nid.uID;
			added.guid = guid;

			DWORD pid = 0;
			GetWindowThreadProcessId(owner, &pid);
			added.processId = pid;

			this->icons.push_back(added);
			icon = std::prev(this->icons.end());
		}
		break;
	case NIM_MODIFY:
		if (icon == this->icons.end()) return;
		break;
	case NIM_SETVERSION:
		if (icon == this->icons.end()) return;
		icon->version = nid.uVersion;
		this->postUpdate(*icon);
		return;
	case NIM_DELETE:
		if (icon == this->icons.end()) return;
		this->postRemove(icon->key);
		this->icons.erase(icon);
		return;
	default: return;
	}

	if (nid.uFlags & NIF_MESSAGE) icon->callbackMessage = nid.uCallbackMessage;

	// The owner is free to destroy the icon once Shell_NotifyIcon returns.
	if (nid.uFlags & NIF_ICON) {
		auto* hicon = static_cast<HICON>(LongToHandle(static_cast<LONG>(nid.hIcon)));
		icon->icon = hicon ? QImage::fromHICON(hicon) : QImage();
	}

	if (nid.uFlags & NIF_TIP) {
		auto length = wcsnlen(nid.szTip, std::size(nid.szTip));
		icon->tooltip = QString::fromWCharArray(nid.szTip, static_cast<qsizetype>(length));
	}

	if ((nid.uFlags & NIF_STATE) && (nid.dwStateMask & NIS_HIDDEN)) {
		icon->hidden = nid.dwState & NIS_HIDDEN;
	}

	this->postUpdate(*icon);
}

void NotifyIconServicePrivate::removeStale() {
	for (auto icon = this->icons.begin(); icon != this->icons.end();) {
		if (IsWindow(reinterpret_cast<HWND>(icon->hwnd))) { // NOLINT
			++icon;
			continue;
		}

		this->postRemove(icon->key);
		icon = this->icons.erase(icon);
	}
}

void NotifyIconServicePrivate::postUpdate(const NotifyIconData& icon) {
	QMetaObject::invokeMethod(
	    this->service,
	    [service = this->service, icon]() { service->updateIcon(icon); },
	    Qt::QueuedConnection
	);
}

void NotifyIconServicePrivate::postRemove(quint64 key) {
	QMetaObject::invokeMethod(
	    this->service,
	    [service = this->service, key]() { service->removeIcon(key); },
	    Qt::QueuedConnection
	);
}

NotifyIconService::NotifyIconService(): impl(new NotifyIconServicePrivate(this)) {
	std::thread([impl = this->impl]() { impl->run(); }).detach();
}

NotifyIconService* NotifyIconService::instance() {
	static auto* instance = new NotifyIconService(); // NOLINT
	return instance;
}

void NotifyIconService::updateIcon(const NotifyIconData& icon) {
	auto existing = std::ranges::find(this->mIcons, icon.key, &NotifyIconData::key);

	if (existing == this->mIcons.end()) {
		qCDebug(logNotifyIcons) << "Added" << icon;
		this->mIcons.append(icon);
		emit this->iconAdded(icon);
	} else {
		qCDebug(logNotifyIcons) << "Changed" << icon;
		*existing = icon;
		emit this->iconChanged(icon);
	}
}

void NotifyIconService::removeIcon(quint64 key) {
	auto existing = std::ranges::find(this->mIcons, key, &NotifyIconData::key);
	if (existing == this->mIcons.end()) return;

	qCDebug(logNotifyIcons) << "Removed" << *existing;
	this->mIcons.erase(existing);
	emit this->iconRemoved(key);
}

} // namespace qs::windows
