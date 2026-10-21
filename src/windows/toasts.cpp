#include <algorithm>
#include <chrono>
#include <utility>

// The other Windows headers need it first.
// clang-format off
#include <windows.h>
// clang-format on

#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qdir.h>
#include <qelapsedtimer.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qset.h>
#include <qstring.h>
#include <qtimezone.h>
#include <qtypes.h>
#include <qurl.h>
#include <qxmlstream.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winsqlite/winsqlite3.h>

#include "../core/logcat.hpp"
#include "toasts.hpp"

namespace wam = winrt::Windows::ApplicationModel;
namespace wf = winrt::Windows::Foundation;
namespace wss = winrt::Windows::Storage::Streams;
namespace wun = winrt::Windows::UI::Notifications;
namespace wunm = winrt::Windows::UI::Notifications::Management;

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logToasts, "quickshell.windows.toasts", QtWarningMsg);

using Listener = wunm::UserNotificationListener;

// WNF_SHEL_NOTIFICATION_TOTAL, the notification center's toast count.
constexpr quint64 WNF_SHEL_NOTIFICATION_TOTAL = 0x0D83063EA3B8D035;

using WnfCallback = LONG(NTAPI*)(
    quint64 stateName,
    ULONG changeStamp,
    void* typeId,
    void* context,
    const void* buffer,
    ULONG bufferSize
);

using RtlSubscribeWnfStateChangeNotification = LONG(NTAPI*)(
    void** subscription,
    quint64 stateName,
    ULONG changeStamp,
    WnfCallback callback,
    void* context,
    void* typeId,
    ULONG serializationGroup,
    ULONG flags
);

// WNF covers changes, polling catches toasts expiring.
constexpr int POLL_INTERVAL = 30000;
constexpr int POLL_INTERVAL_WITHOUT_WNF = 5000;

constexpr int LOGO_SIZE = 64;
constexpr quint64 MAX_LOGO_SIZE = 4 * 1024 * 1024;

QString fromHString(const winrt::hstring& string) {
	return QString::fromWCharArray(string.c_str(), static_cast<qsizetype>(string.size()));
}

QDateTime toQDateTime(wf::DateTime time) {
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	    winrt::clock::to_sys(time).time_since_epoch()
	);

	return QDateTime::fromMSecsSinceEpoch(ms.count(), QTimeZone::UTC);
}

// The database stores times as FILETIMEs, like DateTime.
QDateTime fromFileTime(qint64 ticks) {
	return ticks > 0 ? toQDateTime(wf::DateTime(wf::TimeSpan(ticks))) : QDateTime();
}

// Unpackaged apps without an AppUserModelID go by their program's path.
QString fallbackAppName(const QString& aumid) {
	if (aumid.contains('/') || aumid.contains('\\')) return QFileInfo(aumid).completeBaseName();
	return aumid;
}

QImage readImage(const wss::RandomAccessStreamReference& reference) {
	auto stream = reference.OpenReadAsync().get();
	auto size = static_cast<quint32>(std::min(stream.Size(), MAX_LOGO_SIZE));

	auto reader = wss::DataReader(stream);
	size = reader.LoadAsync(size).get();
	auto data = QByteArray(size, Qt::Uninitialized);
	reader.ReadBytes(
	    winrt::array_view<quint8>(reinterpret_cast<quint8*>(data.data()), size) // NOLINT
	);

	return QImage::fromData(data);
}

// Toasts may name images by ms-appx or http urls, which are not read.
QString localPath(const QString& src) {
	if (src.startsWith("file:", Qt::CaseInsensitive)) return QUrl(src).toLocalFile();
	if (QDir::isAbsolutePath(src)) return src;
	return QString();
}

void parsePayload(const QByteArray& payload, ToastData& data) {
	auto reader = QXmlStreamReader(payload);
	auto bindings = 0;

	while (!reader.atEnd()) {
		reader.readNext();
		if (!reader.isStartElement()) continue;

		auto name = reader.name();
		auto attributes = reader.attributes();
		auto attribute = [&attributes](const char* name) {
			return attributes.value(QLatin1StringView(name)).toString();
		};

		// Older toast templates have their own names, but the same elements.
		if (name == u"binding") {
			bindings++;
		} else if (name == u"toast") {
			data.launch = attribute("launch");
			data.launchType = attribute("activationType").toLower();
			data.scenario = attribute("scenario");
			if (data.scenario.compare(u"default", Qt::CaseInsensitive) == 0) data.scenario.clear();
			data.duration = attribute("duration");
		} else if (bindings == 1 && name == u"text") {
			data.texts.append(reader.readElementText(QXmlStreamReader::IncludeChildElements));
		} else if (bindings == 1 && name == u"image") {
			auto src = attribute("src");

			if (attribute("placement") == u"appLogoOverride") {
				auto path = localPath(src);
				auto logo = path.isEmpty() ? QImage() : QImage(path);
				if (!logo.isNull()) data.logo = logo;
			} else if (data.image.isEmpty()) {
				data.image = src;
			}
		} else if (name == u"action") {
			data.actions.append({
			    .content = attribute("content"),
			    .arguments = attribute("arguments"),
			    .activationType = attribute("activationType").toLower(),
			    .inputId = attribute("hint-inputId"),
			});
		} else if (name == u"input") {
			data.inputs.append({
			    .id = attribute("id"),
			    .type = attribute("type"),
			    .placeholder = attribute("placeHolderContent"),
			    .defaultInput = attribute("defaultInput"),
			});
		}
	}

	if (reader.hasError()) {
		qCDebug(logToasts) << "Could not parse the XML of toast" << data.id << reader.errorString();
	}

	if (data.launchType.isEmpty()) data.launchType = "foreground";

	for (auto& action: data.actions) {
		if (action.activationType.isEmpty()) action.activationType = "foreground";
	}
}

struct AppData {
	QString name;
	QImage logo;
};

AppData readApp(const wam::AppInfo& info) {
	auto app = AppData();

	try {
		if (auto display = info.DisplayInfo()) {
			app.name = fromHString(display.DisplayName());
			auto logo = display.GetLogo(wf::Size(LOGO_SIZE, LOGO_SIZE));
			if (logo) app.logo = readImage(logo);
		}
	} catch (const winrt::hresult_error& e) {
		qCDebug(logToasts) << "No app info for" << fromHString(info.AppUserModelId())
		                   << fromHString(e.message());
	}

	return app;
}

} // namespace

class ToastServicePrivate {
public:
	static LONG NTAPI wnfChanged(
	    quint64 /*stateName*/,
	    ULONG /*changeStamp*/,
	    void* /*typeId*/,
	    void* context,
	    const void* /*buffer*/,
	    ULONG /*bufferSize*/
	) {
		// Called on one of ntdll's threads.
		auto* service = static_cast<ToastService*>(context);
		QMetaObject::invokeMethod(
		    service,
		    [service]() { service->requestSnapshot("WNF"); },
		    Qt::QueuedConnection
		);

		return 0;
	}

	bool subscribeWnf(ToastService* service) {
		auto* ntdll = GetModuleHandleW(L"ntdll.dll");
		auto subscribe = reinterpret_cast<RtlSubscribeWnfStateChangeNotification>( // NOLINT
		    GetProcAddress(ntdll, "RtlSubscribeWnfStateChangeNotification")
		);

		if (!subscribe) {
			qCWarning(logToasts) << "Could not watch the notification center, polling instead.";
			return false;
		}

		auto status = subscribe(
		    &this->wnfSubscription,
		    WNF_SHEL_NOTIFICATION_TOTAL,
		    0,
		    &ToastServicePrivate::wnfChanged,
		    service,
		    nullptr,
		    0,
		    0
		);

		if (status < 0) {
			qCWarning(logToasts) << "Could not watch the notification center, polling instead:" << Qt::hex
			                     << static_cast<ULONG>(status);
			return false;
		}

		return true;
	}

	// Only the database has the toasts' XML, and listing them through the listener takes
	// around 200 ms per toast.
	bool readDatabase(const QSet<quint32>& known, QList<quint32>& ids, QList<ToastData>& added) {
		auto path = QDir::toNativeSeparators(
		    qEnvironmentVariable("LOCALAPPDATA") + "/Microsoft/Windows/Notifications/wpndatabase.db"
		);

		sqlite3* db = nullptr;
		if (sqlite3_open_v2(path.toUtf8().constData(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
		{
			this->databaseFailed(path, db);
			sqlite3_close(db);
			return false;
		}

		sqlite3_busy_timeout(db, 500);

		const auto* query =
		    "select n.Id, n.Payload, n.Tag, n.\"Group\", n.ArrivalTime, n.ExpiryTime, h.PrimaryId "
		    "from Notification n join NotificationHandler h on h.RecordId = n.HandlerId "
		    "where n.Type = 'toast' and n.ExpiryTime > ?";

		sqlite3_stmt* statement = nullptr;
		if (sqlite3_prepare_v2(db, query, -1, &statement, nullptr) != SQLITE_OK) {
			this->databaseFailed(path, db);
			sqlite3_close(db);
			return false;
		}

		sqlite3_bind_int64(statement, 1, winrt::clock::now().time_since_epoch().count());

		auto text = [statement](int column) {
			return QString::fromUtf8(
			    reinterpret_cast<const char*>(sqlite3_column_text(statement, column)) // NOLINT
			);
		};

		auto rowIds = QList<quint32>();
		auto rows = QList<ToastData>();
		auto result = SQLITE_OK;

		while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
			auto id = static_cast<quint32>(sqlite3_column_int64(statement, 0));
			rowIds.append(id);
			if (known.contains(id)) continue;

			auto toast = ToastData();
			toast.id = id;
			toast.tag = text(2);
			toast.group = text(3);
			toast.arrival = fromFileTime(sqlite3_column_int64(statement, 4));
			toast.expiry = fromFileTime(sqlite3_column_int64(statement, 5));
			toast.aumid = text(6);

			auto payload = QByteArray(
			    static_cast<const char*>(sqlite3_column_blob(statement, 1)),
			    sqlite3_column_bytes(statement, 1)
			);

			parsePayload(payload, toast);
			rows.append(toast);
		}

		auto ok = result == SQLITE_DONE;
		if (ok) {
			ids = rowIds;
			added = rows;
		} else {
			this->databaseFailed(path, db);
		}

		sqlite3_finalize(statement);
		sqlite3_close(db);
		return ok;
	}

	void databaseFailed(const QString& path, sqlite3* db) {
		auto error = db ? QString::fromUtf8(sqlite3_errmsg(db)) : QStringLiteral("out of memory");

		if (this->databaseWarned) {
			qCDebug(logToasts) << "Could not read" << path << error;
		} else {
			qCWarning(logToasts) << "Could not read" << path
			                     << "so toasts are slow to list and have no actions:" << error;
			this->databaseWarned = true;
		}
	}

	void readListener(
	    const Listener& listener,
	    const QSet<quint32>& known,
	    QList<quint32>& ids,
	    QList<ToastData>& added
	) {
		auto notifications = listener.GetNotificationsAsync(wun::NotificationKinds::Toast).get();

		for (const auto& notification: notifications) {
			auto id = notification.Id();
			ids.append(id);
			if (known.contains(id)) continue;

			auto toast = ToastData();
			toast.id = id;
			toast.arrival = toQDateTime(notification.CreationTime());

			if (auto info = notification.AppInfo()) {
				toast.aumid = fromHString(info.AppUserModelId());
				if (!this->apps.contains(toast.aumid)) this->apps.insert(toast.aumid, readApp(info));
			}

			if (auto content = notification.Notification()) {
				if (auto expiry = content.ExpirationTime()) toast.expiry = toQDateTime(expiry.Value());

				auto visual = content.Visual();
				auto binding =
				    visual ? visual.GetBinding(wun::KnownNotificationBindings::ToastGeneric()) : nullptr;

				if (binding) {
					for (const auto& text: binding.GetTextElements()) {
						toast.texts.append(fromHString(text.Text()));
					}
				}
			}

			added.append(toast);
		}
	}

	// Only the listener knows app names and logos, so they are read once per app.
	void applyApp(const Listener& listener, ToastData& toast) {
		auto app = this->apps.find(toast.aumid);

		if (app == this->apps.end()) {
			try {
				auto notification = listener.GetNotification(toast.id);
				auto info = notification ? notification.AppInfo() : nullptr;
				if (info) app = this->apps.insert(toast.aumid, readApp(info));
			} catch (const winrt::hresult_error& e) {
				qCDebug(logToasts) << "No app info for toast" << toast.id << fromHString(e.message());
			}
		}

		if (app != this->apps.end()) {
			toast.appName = app->name;
			if (toast.logo.isNull()) toast.logo = app->logo;
		}

		if (toast.appName.isEmpty()) toast.appName = fallbackAppName(toast.aumid);
	}

	static winrt::fire_and_forget snapshot(ToastService* service, QSet<quint32> known) {
		co_await winrt::resume_background();

		auto timer = QElapsedTimer();
		timer.start();

		auto* impl = service->impl;
		auto ids = QList<quint32>();
		auto added = QList<ToastData>();
		auto allowed = false;

		try {
			auto listener = Listener::Current();

			// The database can be read without access, but the listener cannot.
			allowed = listener.GetAccessStatus() == wunm::UserNotificationListenerAccessStatus::Allowed;
			if (allowed) {
				if (!impl->readDatabase(known, ids, added)) impl->readListener(listener, known, ids, added);
				for (auto& toast: added) impl->applyApp(listener, toast);
			}

			impl->listWarned = false;
		} catch (const winrt::hresult_error& e) {
			if (impl->listWarned) {
				qCDebug(logToasts) << "Could not list toasts:" << fromHString(e.message());
			} else {
				qCWarning(logToasts) << "Could not list toasts:" << fromHString(e.message());
				impl->listWarned = true;
			}

			QMetaObject::invokeMethod(service, &ToastService::finishSnapshot, Qt::QueuedConnection);
			co_return;
		}

		QMetaObject::invokeMethod(
		    service,
		    [service, ids, added, allowed, elapsed = timer.elapsed()]() {
			    service->applySnapshot(ids, added, allowed, elapsed);
		    },
		    Qt::QueuedConnection
		);
	}

	void* wnfSubscription = nullptr;

	// Only used by the running snapshot.
	QHash<QString, AppData> apps;
	bool databaseWarned = false;
	bool listWarned = false;
};

ToastService::ToastService(): impl(new ToastServicePrivate()) {
	try {
		auto listener = Listener::Current();
		this->mAccessStatus = static_cast<AccessStatus>(listener.GetAccessStatus());

		// Asks the user through Windows if they were never asked.
		if (this->mAccessStatus == Unspecified) {
			listener.RequestAccessAsync().Completed(
			    [this](
			        const wf::IAsyncOperation<wunm::UserNotificationListenerAccessStatus>& op,
			        wf::AsyncStatus status
			    ) {
				    if (status != wf::AsyncStatus::Completed) return;

				    auto access = static_cast<AccessStatus>(op.GetResults());
				    QMetaObject::invokeMethod(
				        this,
				        [this, access]() {
					        this->setAccessStatus(access);
					        this->requestSnapshot("access");
				        },
				        Qt::QueuedConnection
				    );
			    }
			);
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToasts) << "Could not access notifications:" << fromHString(e.message());
	}

	if (this->mAccessStatus == Denied) {
		qCWarning(logToasts) << "Apps may not read notifications. Allow it in Settings, Privacy & "
		                        "security, Notifications.";
	}

	auto wnf = this->impl->subscribeWnf(this);

	this->pollTimer.setInterval(wnf ? POLL_INTERVAL : POLL_INTERVAL_WITHOUT_WNF);
	QObject::connect(&this->pollTimer, &QTimer::timeout, this, [this]() {
		this->requestSnapshot("poll");
	});
	this->pollTimer.start();

	this->requestSnapshot("start");
}

ToastService* ToastService::instance() {
	static auto* instance = new ToastService(); // NOLINT
	return instance;
}

void ToastService::setAccessStatus(AccessStatus status) {
	if (status == this->mAccessStatus) return;
	this->mAccessStatus = status;
	emit this->accessStatusChanged();
}

void ToastService::requestSnapshot(const char* source) {
	if (this->snapshotRunning) {
		this->pendingSource = source;
		return;
	}

	this->snapshotRunning = true;
	qCDebug(logToasts) << "Snapshot from" << source;

	auto known = QSet<quint32>();
	for (const auto& toast: this->mToasts) known.insert(toast.id);
	ToastServicePrivate::snapshot(this, known);
}

void ToastService::finishSnapshot() {
	this->snapshotRunning = false;

	try {
		this->setAccessStatus(static_cast<AccessStatus>(Listener::Current().GetAccessStatus()));
	} catch (const winrt::hresult_error& e) {
		qCDebug(logToasts) << "Could not read access status:" << fromHString(e.message());
	}

	if (auto* source = std::exchange(this->pendingSource, nullptr)) this->requestSnapshot(source);
}

void ToastService::applySnapshot(
    const QList<quint32>& ids,
    QList<ToastData> added,
    bool allowed,
    qint64 elapsed
) {
	auto byArrival = [](const ToastData& a, const ToastData& b) {
		return a.arrival != b.arrival ? a.arrival < b.arrival : a.id < b.id;
	};

	std::ranges::sort(added, byArrival);

	auto current = QSet<quint32>(ids.begin(), ids.end());
	auto removed = QList<ToastData>();
	this->mToasts.removeIf([&](const ToastData& toast) {
		if (current.contains(toast.id)) return false;
		removed.append(toast);
		return true;
	});

	this->mToasts.append(added);
	std::ranges::stable_sort(this->mToasts, byArrival);

	qCDebug(logToasts) << "Snapshot of" << ids.length() << "toasts took" << elapsed << "ms";

	// Toasts already there once access is allowed are not new, like those of the first snapshot.
	if (!std::exchange(this->listed, allowed)) {
		auto first = !std::exchange(this->mReady, true);
		this->finishSnapshot();
		if (first) emit this->ready();
		return;
	}

	auto replaced = QList<std::pair<quint32, ToastData>>();
	for (const auto& toast: added) {
		auto old = std::ranges::find_if(removed, [&toast](const ToastData& old) {
			return !toast.tag.isEmpty() && old.tag == toast.tag && old.group == toast.group
			    && old.aumid == toast.aumid;
		});

		if (old == removed.end()) continue;
		replaced.append({old->id, toast});
		removed.erase(old);
	}

	this->finishSnapshot();

	auto now = QDateTime::currentDateTimeUtc();
	for (const auto& toast: removed) {
		auto expired = toast.expiry.isValid() && toast.expiry <= now;
		qCDebug(logToasts) << "Removed" << toast.id << toast.aumid << "expired" << expired;
		emit this->toastRemoved(toast.id, expired);
	}

	for (const auto& toast: added) {
		auto old =
		    std::ranges::find(replaced, toast.id, [](const auto& pair) { return pair.second.id; });
		auto change = old == replaced.end()
		                ? QStringLiteral("Added %1").arg(toast.id)
		                : QStringLiteral("Replaced %1 with %2").arg(old->first).arg(toast.id);

		qCDebug(logToasts).nospace() << qPrintable(change) << " " << toast.aumid << " ("
		                             << toast.appName << ") texts " << toast.texts << " tag "
		                             << toast.tag << " group " << toast.group << " launch "
		                             << toast.launch << " (" << toast.launchType << ") scenario "
		                             << toast.scenario << " duration " << toast.duration << " image "
		                             << toast.image << " logo " << toast.logo.size() << " arrival "
		                             << toast.arrival << " expiry " << toast.expiry;

		for (const auto& action: toast.actions) {
			qCDebug(logToasts) << "  action" << action.content << action.arguments
			                   << action.activationType << action.inputId;
		}

		for (const auto& input: toast.inputs) {
			qCDebug(logToasts) << "  input" << input.id << input.type << input.placeholder
			                   << input.defaultInput;
		}

		if (old == replaced.end()) emit this->toastAdded(toast);
		else emit this->toastReplaced(old->first, toast);
	}
}

void ToastService::remove(quint32 id) {
	try {
		Listener::Current().RemoveNotification(id);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToasts) << "Could not remove toast" << id << fromHString(e.message());
	}
}

void ToastService::clear() {
	try {
		Listener::Current().ClearNotifications();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logToasts) << "Could not clear toasts:" << fromHString(e.message());
	}
}

} // namespace qs::windows
