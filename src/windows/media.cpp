#include <algorithm>
#include <array>
#include <chrono>
#include <utility>
#include <vector>

// The other Windows headers need it first.
// clang-format off
#include <windows.h>
// clang-format on

#include <appmodel.h>
#include <propidl.h>
#include <propsys.h>
#include <qbytearray.h>
#include <qcoreapplication.h>
#include <qdatetime.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmutex.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtimezone.h>
#include <qtypes.h>
#include <shellapi.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include "../core/logcat.hpp"
#include "../core/model.hpp"
#include "hwnd.hpp"
#include "media.hpp"

namespace wf = winrt::Windows::Foundation;
namespace wm = winrt::Windows::Media;
namespace wmc = winrt::Windows::Media::Control;
namespace wss = winrt::Windows::Storage::Streams;

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logMedia, "quickshell.windows.media", QtWarningMsg);

using Session = wmc::GlobalSystemMediaTransportControlsSession;
using SessionManager = wmc::GlobalSystemMediaTransportControlsSessionManager;

// Thumbnails are a few hundred KiB at most, anything larger is not one.
constexpr quint64 MAX_THUMBNAIL_SIZE = 16 * 1024 * 1024;

// The SDK's key needs propkey.h with INITGUID.
constexpr PROPERTYKEY KEY_APP_USER_MODEL_ID = {
    {0x9f4c2855, 0x9f79, 0x4b39, {0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3}},
    5
};

QString fromHString(const winrt::hstring& string) {
	return QString::fromWCharArray(string.c_str(), static_cast<qsizetype>(string.size()));
}

qreal seconds(wf::TimeSpan span) { return std::chrono::duration<qreal>(span).count(); }

QString withoutExe(const QString& name) {
	return name.endsWith(".exe", Qt::CaseInsensitive) ? name.chopped(4) : name;
}

QString appDisplayName(const winrt::hstring& aumid) {
	try {
		auto name = winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(aumid)
		                .DisplayInfo()
		                .DisplayName();
		if (!name.empty()) return fromHString(name);
	} catch (const winrt::hresult_error& e) {
		qCDebug(logMedia) << "No app info for" << fromHString(aumid) << fromHString(e.message());
	}

	return withoutExe(fromHString(aumid));
}

void logRequest(const wf::IAsyncOperation<bool>& operation, const char* request) {
	operation.Completed([request](const wf::IAsyncOperation<bool>& op, wf::AsyncStatus status) {
		if (status != wf::AsyncStatus::Completed || !op.GetResults()) {
			qCDebug(logMedia) << "The app did not accept" << request;
		}
	});
}

// Controls are only requests, which the app may refuse.
template <typename Call>
void request(const Session& session, const char* name, Call call) {
	try {
		logRequest(call(session), name);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not request" << name << fromHString(e.message());
	}
}

struct WindowSearch {
	QString aumid;
	QString program;
	HWND found = nullptr;
};

QString windowAppId(HWND hwnd) {
	IPropertyStore* store = nullptr;
	if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store)))) return QString();

	QString id;
	PROPVARIANT value;
	PropVariantInit(&value);
	if (SUCCEEDED(store->GetValue(KEY_APP_USER_MODEL_ID, &value)) && value.vt == VT_LPWSTR) {
		id = QString::fromWCharArray(value.pwszVal);
	}

	PropVariantClear(&value);
	store->Release();
	return id;
}

// The package's AppUserModelID for packaged apps, otherwise the program's file name.
void processIds(HWND hwnd, QString& aumid, QString& program) {
	DWORD pid = 0;
	GetWindowThreadProcessId(hwnd, &pid);
	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) return;

	auto buffer = std::array<wchar_t, MAX_PATH> {};
	auto length = static_cast<UINT32>(buffer.size());
	if (GetApplicationUserModelId(process, &length, buffer.data()) == ERROR_SUCCESS) {
		aumid = QString::fromWCharArray(buffer.data());
	}

	auto pathLength = static_cast<DWORD>(buffer.size());
	if (QueryFullProcessImageNameW(process, 0, buffer.data(), &pathLength)) {
		program = QFileInfo(QString::fromWCharArray(buffer.data(), pathLength)).fileName();
	}

	CloseHandle(process);
}

// Top level windows come in z-order, so the first match is the frontmost.
BOOL CALLBACK findAppWindow(HWND hwnd, LPARAM param) {
	auto* search = reinterpret_cast<WindowSearch*>(param); // NOLINT

	if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
	if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) return TRUE;

	if (windowAppId(hwnd).compare(search->aumid, Qt::CaseInsensitive) != 0) {
		QString aumid;
		QString program;
		processIds(hwnd, aumid, program);

		if (aumid.compare(search->aumid, Qt::CaseInsensitive) != 0
		    && withoutExe(program).compare(search->program, Qt::CaseInsensitive) != 0)
		{
			return TRUE;
		}
	}

	search->found = hwnd;
	return FALSE;
}

QDateTime toQDateTime(wf::DateTime time) {
	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	    winrt::clock::to_sys(time).time_since_epoch()
	);

	return QDateTime::fromMSecsSinceEpoch(ms.count(), QTimeZone::UTC);
}

} // namespace

QString MediaPlaybackStatus::toString(MediaPlaybackStatus::Enum status) {
	switch (status) {
	case MediaPlaybackStatus::Closed: return "Closed";
	case MediaPlaybackStatus::Opened: return "Opened";
	case MediaPlaybackStatus::Changing: return "Changing";
	case MediaPlaybackStatus::Stopped: return "Stopped";
	case MediaPlaybackStatus::Playing: return "Playing";
	case MediaPlaybackStatus::Paused: return "Paused";
	default: return "Invalid Status";
	}
}

QString MediaPlaybackType::toString(MediaPlaybackType::Enum type) {
	switch (type) {
	case MediaPlaybackType::Unknown: return "Unknown";
	case MediaPlaybackType::Music: return "Music";
	case MediaPlaybackType::Video: return "Video";
	case MediaPlaybackType::Image: return "Image";
	default: return "Invalid Type";
	}
}

QString MediaAutoRepeatMode::toString(MediaAutoRepeatMode::Enum mode) {
	switch (mode) {
	case MediaAutoRepeatMode::None: return "None";
	case MediaAutoRepeatMode::Track: return "Track";
	case MediaAutoRepeatMode::List: return "List";
	default: return "Invalid Mode";
	}
}

void MediaThumbnail::setImage(QImage image) {
	{
		auto lock = QMutexLocker(&this->mutex);
		this->image = std::move(image);
	}

	this->imageChanged();
}

QImage
MediaThumbnail::requestImage(const QString& /*unused*/, QSize* size, const QSize& /*unused*/) {
	auto lock = QMutexLocker(&this->mutex);
	if (size != nullptr) *size = this->image.size();
	return this->image;
}

struct MediaPropertiesData {
	QString title;
	QString subtitle;
	QString artist;
	QString albumTitle;
	QString albumArtist;
	qint32 trackNumber = 0;
	qint32 albumTrackCount = 0;
	QList<QString> genres;
	QByteArray thumbnailData;
	QImage thumbnail;
};

class MediaSessionPrivate {
public:
	explicit MediaSessionPrivate(Session session): session(std::move(session)) {}

	// Media properties are only available asynchronously, and the thumbnail is a stream.
	static winrt::fire_and_forget
	loadMediaProperties(Session session, QPointer<MediaSession> target, quint64 generation) {
		co_await winrt::resume_background();

		auto data = MediaPropertiesData();
		try {
			auto properties = co_await session.TryGetMediaPropertiesAsync();
			if (properties) {
				data.title = fromHString(properties.Title());
				data.subtitle = fromHString(properties.Subtitle());
				data.artist = fromHString(properties.Artist());
				data.albumTitle = fromHString(properties.AlbumTitle());
				data.albumArtist = fromHString(properties.AlbumArtist());
				data.trackNumber = properties.TrackNumber();
				data.albumTrackCount = properties.AlbumTrackCount();

				for (const auto& genre: properties.Genres()) {
					data.genres.append(fromHString(genre));
				}

				if (auto thumbnail = properties.Thumbnail()) {
					auto stream = co_await thumbnail.OpenReadAsync();
					auto size = static_cast<quint32>(std::min(stream.Size(), MAX_THUMBNAIL_SIZE));

					auto reader = wss::DataReader(stream);
					size = co_await reader.LoadAsync(size);
					data.thumbnailData.resize(size);
					reader.ReadBytes(
					    winrt::array_view<quint8>(
					        reinterpret_cast<quint8*>(data.thumbnailData.data()), // NOLINT
					        size
					    )
					);

					data.thumbnail = QImage::fromData(data.thumbnailData);
				}
			}
		} catch (const winrt::hresult_error& e) {
			qCWarning(logMedia) << "Could not read track info:" << fromHString(e.message());
		}

		QMetaObject::invokeMethod(
		    QCoreApplication::instance(),
		    [target, generation, data]() {
			    if (target) target->applyMediaProperties(generation, data);
		    },
		    Qt::QueuedConnection
		);
	}

	Session session;
	Session::MediaPropertiesChanged_revoker mediaPropertiesChanged;
	Session::PlaybackInfoChanged_revoker playbackInfoChanged;
	Session::TimelinePropertiesChanged_revoker timelinePropertiesChanged;
	QByteArray thumbnailData;
};

MediaSession::MediaSession(MediaSessionPrivate* impl, QObject* parent)
    : QObject(parent)
    , impl(impl) {
	auto& session = this->impl->session;

	try {
		auto aumid = session.SourceAppUserModelId();
		this->mSourceAppUserModelId = fromHString(aumid);
		this->mAppName = appDisplayName(aumid);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not read media session app:" << fromHString(e.message());
	}

	// Session events arrive on the thread pool.
	auto self = QPointer(this);
	auto post = [self](void (MediaSession::*method)()) {
		return [self, method](auto&&, auto&&) {
			QMetaObject::invokeMethod(
			    QCoreApplication::instance(),
			    [self, method]() {
				    if (self) (self.data()->*method)();
			    },
			    Qt::QueuedConnection
			);
		};
	};

	try {
		this->impl->mediaPropertiesChanged = session.MediaPropertiesChanged(
		    winrt::auto_revoke,
		    post(&MediaSession::requestMediaProperties)
		);
		this->impl->playbackInfoChanged =
		    session.PlaybackInfoChanged(winrt::auto_revoke, post(&MediaSession::refreshPlaybackInfo));
		this->impl->timelinePropertiesChanged =
		    session.TimelinePropertiesChanged(winrt::auto_revoke, post(&MediaSession::refreshTimeline));
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not watch media session" << this->mSourceAppUserModelId
		                    << fromHString(e.message());
	}

	this->refreshPlaybackInfo();
	this->refreshTimeline();
	this->requestMediaProperties();
}

MediaSession::~MediaSession() { delete this->impl; }

void MediaSession::requestMediaProperties() {
	MediaSessionPrivate::loadMediaProperties(
	    this->impl->session,
	    QPointer(this),
	    ++this->mediaGeneration
	);
}

void MediaSession::applyMediaProperties(quint64 generation, const MediaPropertiesData& data) {
	// An older request finishing late.
	if (generation != this->mediaGeneration) return;

	Qt::beginPropertyUpdateGroup();
	this->bTitle = data.title;
	this->bSubtitle = data.subtitle;
	this->bArtist = data.artist;
	this->bAlbumTitle = data.albumTitle;
	this->bAlbumArtist = data.albumArtist;
	this->bTrackNumber = data.trackNumber;
	this->bAlbumTrackCount = data.albumTrackCount;
	this->bGenres = data.genres;

	// Apps resend unchanged thumbnails with other changes, and a new url would reload it.
	if (data.thumbnailData != this->impl->thumbnailData) {
		this->impl->thumbnailData = data.thumbnailData;
		this->thumbnail.setImage(data.thumbnail);
		this->bThumbnailUrl = data.thumbnail.isNull() ? QString() : this->thumbnail.url();
	}
	Qt::endPropertyUpdateGroup();

	emit this->mediaPropertiesChanged();
}

void MediaSession::refreshPlaybackInfo() {
	wmc::GlobalSystemMediaTransportControlsSessionPlaybackInfo info {nullptr};
	try {
		info = this->impl->session.GetPlaybackInfo();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not read playback info of" << this->mSourceAppUserModelId
		                    << fromHString(e.message());
		return;
	}

	auto controls = info.Controls();
	auto status = static_cast<MediaPlaybackStatus::Enum>(info.PlaybackStatus());
	auto type = info.PlaybackType();
	auto repeat = info.AutoRepeatMode();
	auto rate = info.PlaybackRate();
	auto shuffle = info.IsShuffleActive();
	auto newRate = rate ? rate.Value() : 1.0;

	// Continue the estimate from where it is now, as the app may not report a new position.
	auto positionMoved = false;
	if (status != this->bPlaybackStatus || newRate != this->bPlaybackRate) {
		this->mPosition = this->position();
		this->mPositionTime = QDateTime::currentDateTimeUtc();
		positionMoved = true;
	}

	Qt::beginPropertyUpdateGroup();
	this->bPlaybackStatus = status;
	this->bPlaybackType =
	    type ? static_cast<MediaPlaybackType::Enum>(type.Value()) : MediaPlaybackType::Unknown;
	this->bPlayEnabled = controls.IsPlayEnabled();
	this->bPauseEnabled = controls.IsPauseEnabled();
	this->bStopEnabled = controls.IsStopEnabled();
	this->bNextEnabled = controls.IsNextEnabled();
	this->bPreviousEnabled = controls.IsPreviousEnabled();
	this->bFastForwardEnabled = controls.IsFastForwardEnabled();
	this->bRewindEnabled = controls.IsRewindEnabled();
	this->bPlaybackPositionEnabled = controls.IsPlaybackPositionEnabled();
	this->bPlaybackRateEnabled = controls.IsPlaybackRateEnabled();
	this->bRepeatEnabled = controls.IsRepeatEnabled();
	this->bShuffleEnabled = controls.IsShuffleEnabled();
	this->bAutoRepeatMode =
	    repeat ? static_cast<MediaAutoRepeatMode::Enum>(repeat.Value()) : MediaAutoRepeatMode::None;
	this->bShuffleActive = shuffle && shuffle.Value();
	this->bPlaybackRate = newRate;
	Qt::endPropertyUpdateGroup();

	if (positionMoved) emit this->positionChanged();
}

void MediaSession::refreshTimeline() {
	wmc::GlobalSystemMediaTransportControlsSessionTimelineProperties timeline {nullptr};
	try {
		timeline = this->impl->session.GetTimelineProperties();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not read the timeline of" << this->mSourceAppUserModelId
		                    << fromHString(e.message());
		return;
	}

	Qt::beginPropertyUpdateGroup();
	this->bStartTime = seconds(timeline.StartTime());
	this->bEndTime = seconds(timeline.EndTime());
	this->bMinSeekTime = seconds(timeline.MinSeekTime());
	this->bMaxSeekTime = seconds(timeline.MaxSeekTime());
	Qt::endPropertyUpdateGroup();

	// Apps that never set the time leave it at the epoch (1601).
	auto now = QDateTime::currentDateTimeUtc();
	auto lastUpdated = timeline.LastUpdatedTime();
	auto updated = lastUpdated.time_since_epoch().count() > 0 ? toQDateTime(lastUpdated) : now;
	this->mPosition = seconds(timeline.Position());
	this->mPositionTime = updated < now ? updated : now;
	emit this->positionChanged();
}

qreal MediaSession::position() const {
	auto position = this->mPosition;

	if (this->bPlaybackStatus == MediaPlaybackStatus::Playing && this->mPositionTime.isValid()) {
		auto elapsed = this->mPositionTime.msecsTo(QDateTime::currentDateTimeUtc());
		position += static_cast<qreal>(elapsed) / 1000.0 * this->bPlaybackRate;
	}

	auto end = this->bEndTime.value();
	return end > 0 ? std::clamp(position, this->bStartTime.value(), end) : position;
}

void MediaSession::setPosition(qreal position) {
	if (!this->bPlaybackPositionEnabled) {
		qWarning() << "Cannot set position of" << this << "because playbackPositionEnabled is false.";
		return;
	}

	auto ticks = std::chrono::duration_cast<wf::TimeSpan>(std::chrono::duration<qreal>(position));

	try {
		logRequest(this->impl->session.TryChangePlaybackPositionAsync(ticks.count()), "a position");
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not set position of" << this->mSourceAppUserModelId
		                    << fromHString(e.message());
		return;
	}

	// Apps report the new position later if at all.
	this->mPosition = position;
	this->mPositionTime = QDateTime::currentDateTimeUtc();
	emit this->positionChanged();
}

void MediaSession::setAutoRepeatMode(MediaAutoRepeatMode::Enum mode) {
	if (!this->bRepeatEnabled) {
		qWarning() << "Cannot set autoRepeatMode of" << this << "because repeatEnabled is false.";
		return;
	}

	try {
		logRequest(
		    this->impl->session.TryChangeAutoRepeatModeAsync(
		        static_cast<wm::MediaPlaybackAutoRepeatMode>(mode)
		    ),
		    "a repeat mode"
		);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not set repeat mode:" << fromHString(e.message());
	}
}

void MediaSession::setShuffleActive(bool shuffle) {
	if (!this->bShuffleEnabled) {
		qWarning() << "Cannot set shuffleActive of" << this << "because shuffleEnabled is false.";
		return;
	}

	try {
		logRequest(this->impl->session.TryChangeShuffleActiveAsync(shuffle), "shuffle");
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not set shuffle:" << fromHString(e.message());
	}
}

void MediaSession::setPlaybackRate(qreal rate) {
	if (!this->bPlaybackRateEnabled) {
		qWarning() << "Cannot set playbackRate of" << this << "because playbackRateEnabled is false.";
		return;
	}

	try {
		logRequest(this->impl->session.TryChangePlaybackRateAsync(rate), "a playback rate");
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not set playback rate:" << fromHString(e.message());
	}
}

void MediaSession::play() {
	request(this->impl->session, "play", [](const Session& s) { return s.TryPlayAsync(); });
}

void MediaSession::pause() {
	request(this->impl->session, "pause", [](const Session& s) { return s.TryPauseAsync(); });
}

void MediaSession::stop() {
	request(this->impl->session, "stop", [](const Session& s) { return s.TryStopAsync(); });
}

void MediaSession::togglePlayPause() {
	request(this->impl->session, "toggle play pause", [](const Session& s) {
		return s.TryTogglePlayPauseAsync();
	});
}

void MediaSession::skipNext() {
	request(this->impl->session, "skip next", [](const Session& s) { return s.TrySkipNextAsync(); });
}

void MediaSession::skipPrevious() {
	request(this->impl->session, "skip previous", [](const Session& s) {
		return s.TrySkipPreviousAsync();
	});
}

void MediaSession::fastForward() {
	request(this->impl->session, "fast forward", [](const Session& s) {
		return s.TryFastForwardAsync();
	});
}

void MediaSession::rewind() {
	request(this->impl->session, "rewind", [](const Session& s) { return s.TryRewindAsync(); });
}

bool MediaSession::activate() {
	auto search = WindowSearch();
	search.aumid = this->mSourceAppUserModelId;
	search.program = withoutExe(this->mSourceAppUserModelId);
	EnumWindows(&findAppWindow, reinterpret_cast<LPARAM>(&search)); // NOLINT

	if (!search.found) {
		qCDebug(logMedia) << "No window found for" << this->mSourceAppUserModelId;
		return false;
	}

	return HwndActions::activate(reinterpret_cast<qint64>(search.found)); // NOLINT
}

class MediaServicePrivate {
public:
	SessionManager manager {nullptr};
	SessionManager::SessionsChanged_revoker sessionsChanged;
	SessionManager::CurrentSessionChanged_revoker currentSessionChanged;
};

MediaService::MediaService(): impl(new MediaServicePrivate()) {
	// Requesting the manager takes a moment, and must not block the GUI thread.
	try {
		SessionManager::RequestAsync().Completed([this](
		                                             const wf::IAsyncOperation<SessionManager>& op,
		                                             wf::AsyncStatus status
		                                         ) {
			if (status != wf::AsyncStatus::Completed) {
				qCWarning(logMedia) << "Could not access media sessions.";
				return;
			}

			auto manager = op.GetResults();
			QMetaObject::invokeMethod(
			    this,
			    [this, manager]() {
				    this->impl->manager = manager;

				    // Manager events arrive on the thread pool.
				    auto post = [this](void (MediaService::*method)()) {
					    return [this, method](auto&&, auto&&) {
						    QMetaObject::invokeMethod(this, method, Qt::QueuedConnection);
					    };
				    };

				    try {
					    this->impl->sessionsChanged =
					        manager.SessionsChanged(winrt::auto_revoke, post(&MediaService::refreshSessions));
					    this->impl->currentSessionChanged = manager.CurrentSessionChanged(
					        winrt::auto_revoke,
					        post(&MediaService::refreshCurrentSession)
					    );
				    } catch (const winrt::hresult_error& e) {
					    qCWarning(logMedia) << "Could not watch media sessions:" << fromHString(e.message());
				    }

				    this->refreshSessions();
			    },
			    Qt::QueuedConnection
			);
		});
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not access media sessions:" << fromHString(e.message());
	}
}

MediaService* MediaService::instance() {
	static auto* instance = new MediaService(); // NOLINT
	return instance;
}

void MediaService::refreshSessions() {
	if (!this->impl->manager) return;

	std::vector<Session> current;
	try {
		auto sessions = this->impl->manager.GetSessions();
		current.assign(sessions.begin(), sessions.end());
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not list media sessions:" << fromHString(e.message());
		return;
	}

	auto existing = this->mSessions.valueList();

	for (auto* session: existing) {
		auto found = std::ranges::any_of(current, [session](const Session& s) {
			return s == session->impl->session;
		});

		if (found) continue;

		if (session == this->mCurrentSession) {
			this->mCurrentSession = nullptr;
			emit this->currentSessionChanged();
		}

		this->mSessions.removeObject(session);
		session->deleteLater();
	}

	for (const auto& session: current) {
		auto found = std::ranges::any_of(existing, [&session](MediaSession* s) {
			return s->impl->session == session;
		});

		if (!found) {
			this->mSessions.insertObject(new MediaSession(new MediaSessionPrivate(session), this));
		}
	}

	this->refreshCurrentSession();
}

void MediaService::refreshCurrentSession() {
	if (!this->impl->manager) return;

	MediaSession* found = nullptr;
	try {
		if (auto current = this->impl->manager.GetCurrentSession()) {
			// The current session comes as a new object each time, unlike the listed ones.
			// Apps with several sessions, such as browsers, are told apart by playback status.
			auto aumid = fromHString(current.SourceAppUserModelId());
			auto status =
			    static_cast<MediaPlaybackStatus::Enum>(current.GetPlaybackInfo().PlaybackStatus());

			for (auto* session: this->mSessions.valueList()) {
				if (session->impl->session == current) {
					found = session;
					break;
				}

				if (session->sourceAppUserModelId() != aumid) continue;
				if (!found || (found->bPlaybackStatus != status && session->bPlaybackStatus == status)) {
					found = session;
				}
			}
		}
	} catch (const winrt::hresult_error& e) {
		qCWarning(logMedia) << "Could not read the current media session:" << fromHString(e.message());
	}

	if (found == this->mCurrentSession) return;
	this->mCurrentSession = found;
	emit this->currentSessionChanged();
}

MediaSessionsQml::MediaSessionsQml(QObject* parent): QObject(parent) {
	QObject::connect(
	    MediaService::instance(),
	    &MediaService::currentSessionChanged,
	    this,
	    &MediaSessionsQml::currentSessionChanged
	);
}

} // namespace qs::windows
