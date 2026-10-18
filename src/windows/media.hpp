#pragma once

#include <qcontainerfwd.h>
#include <qdatetime.h>
#include <qimage.h>
#include <qmutex.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/doc.hpp"
#include "../core/imageprovider.hpp"
#include "../core/model.hpp"

namespace qs::windows {

///! Playback status of a MediaSession.
/// See @@MediaSession.playbackStatus.
class MediaPlaybackStatus: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Closed = 0,
		/// The app has opened media but not started it.
		Opened = 1,
		/// The app is switching between tracks or loading.
		Changing = 2,
		Stopped = 3,
		Playing = 4,
		Paused = 5,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::MediaPlaybackStatus::Enum status);
};

///! Kind of media a MediaSession plays.
/// See @@MediaSession.playbackType.
class MediaPlaybackType: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Unknown = 0,
		Music = 1,
		Video = 2,
		Image = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::MediaPlaybackType::Enum type);
};

///! Repeat mode of a MediaSession.
/// See @@MediaSession.autoRepeatMode.
class MediaAutoRepeatMode: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		Track = 1,
		List = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::MediaAutoRepeatMode::Enum mode);
};

// Serves a session's thumbnail, which may be read from the image loader's thread.
class MediaThumbnail: public QsIndexedImageHandle {
public:
	explicit MediaThumbnail(): QsIndexedImageHandle(QQuickImageProvider::Image) {}

	void setImage(QImage image);
	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	QMutex mutex;
	QImage image;
};

struct MediaPropertiesData;
class MediaSessionPrivate;

///! An app's media session, as shown in Windows' media flyout.
/// Track info, playback state and controls of a [GlobalSystemMediaTransportControlsSession].
///
/// Controls are only requests: the app may ignore them, and reports what it did
/// through the properties. Check the matching `xyzEnabled` property first.
///
/// [GlobalSystemMediaTransportControlsSession]: https://learn.microsoft.com/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession
class MediaSession: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("MediaSessions can only be acquired from MediaSessions");
	// clang-format off
	/// The app's [AppUserModelID], such as `Spotify.exe` or `Chrome`.
	///
	/// [AppUserModelID]: https://learn.microsoft.com/windows/win32/shell/appids
	Q_PROPERTY(QString sourceAppUserModelId READ sourceAppUserModelId CONSTANT);
	/// The app's display name, or @@sourceAppUserModelId without `.exe` if Windows has none.
	Q_PROPERTY(QString appName READ appName CONSTANT);
	Q_PROPERTY(QString title READ default NOTIFY titleChanged BINDABLE bindableTitle);
	Q_PROPERTY(QString subtitle READ default NOTIFY subtitleChanged BINDABLE bindableSubtitle);
	Q_PROPERTY(QString artist READ default NOTIFY artistChanged BINDABLE bindableArtist);
	Q_PROPERTY(QString albumTitle READ default NOTIFY albumTitleChanged BINDABLE bindableAlbumTitle);
	Q_PROPERTY(QString albumArtist READ default NOTIFY albumArtistChanged BINDABLE bindableAlbumArtist);
	/// The track's number on its album, or 0 if not provided.
	Q_PROPERTY(qint32 trackNumber READ default NOTIFY trackNumberChanged BINDABLE bindableTrackNumber);
	/// The number of tracks on the album, or 0 if not provided.
	Q_PROPERTY(qint32 albumTrackCount READ default NOTIFY albumTrackCountChanged BINDABLE bindableAlbumTrackCount);
	Q_PROPERTY(QList<QString> genres READ default NOTIFY genresChanged BINDABLE bindableGenres);
	/// An image url for the track's thumbnail, such as album art, or `""` if there is none.
	Q_PROPERTY(QString thumbnailUrl READ default NOTIFY thumbnailUrlChanged BINDABLE bindableThumbnailUrl);
	Q_PROPERTY(qs::windows::MediaPlaybackStatus::Enum playbackStatus READ default NOTIFY playbackStatusChanged BINDABLE bindablePlaybackStatus);
	Q_PROPERTY(qs::windows::MediaPlaybackType::Enum playbackType READ default NOTIFY playbackTypeChanged BINDABLE bindablePlaybackType);
	Q_PROPERTY(bool playEnabled READ default NOTIFY playEnabledChanged BINDABLE bindablePlayEnabled);
	Q_PROPERTY(bool pauseEnabled READ default NOTIFY pauseEnabledChanged BINDABLE bindablePauseEnabled);
	Q_PROPERTY(bool stopEnabled READ default NOTIFY stopEnabledChanged BINDABLE bindableStopEnabled);
	Q_PROPERTY(bool nextEnabled READ default NOTIFY nextEnabledChanged BINDABLE bindableNextEnabled);
	Q_PROPERTY(bool previousEnabled READ default NOTIFY previousEnabledChanged BINDABLE bindablePreviousEnabled);
	Q_PROPERTY(bool fastForwardEnabled READ default NOTIFY fastForwardEnabledChanged BINDABLE bindableFastForwardEnabled);
	Q_PROPERTY(bool rewindEnabled READ default NOTIFY rewindEnabledChanged BINDABLE bindableRewindEnabled);
	/// If @@position can be set.
	Q_PROPERTY(bool playbackPositionEnabled READ default NOTIFY playbackPositionEnabledChanged BINDABLE bindablePlaybackPositionEnabled);
	/// If @@playbackRate can be set.
	Q_PROPERTY(bool playbackRateEnabled READ default NOTIFY playbackRateEnabledChanged BINDABLE bindablePlaybackRateEnabled);
	/// If @@autoRepeatMode can be set.
	Q_PROPERTY(bool repeatEnabled READ default NOTIFY repeatEnabledChanged BINDABLE bindableRepeatEnabled);
	/// If @@shuffleActive can be set.
	Q_PROPERTY(bool shuffleEnabled READ default NOTIFY shuffleEnabledChanged BINDABLE bindableShuffleEnabled);
	/// The repeat mode, `None` if the app does not report one.
	Q_PROPERTY(qs::windows::MediaAutoRepeatMode::Enum autoRepeatMode READ default WRITE setAutoRepeatMode NOTIFY autoRepeatModeChanged BINDABLE bindableAutoRepeatMode);
	/// If the app shuffles, false if it does not report it.
	Q_PROPERTY(bool shuffleActive READ default WRITE setShuffleActive NOTIFY shuffleActiveChanged BINDABLE bindableShuffleActive);
	/// The playback speed as a multiplier, 1 if the app does not report it.
	Q_PROPERTY(qreal playbackRate READ default WRITE setPlaybackRate NOTIFY playbackRateChanged BINDABLE bindablePlaybackRate);
	/// Start of the track's timeline in seconds, usually 0.
	Q_PROPERTY(qreal startTime READ default NOTIFY timelineChanged BINDABLE bindableStartTime);
	/// End of the track's timeline in seconds, or 0 if the app reports no timeline.
	Q_PROPERTY(qreal endTime READ default NOTIFY timelineChanged BINDABLE bindableEndTime);
	Q_PROPERTY(qreal minSeekTime READ default NOTIFY timelineChanged BINDABLE bindableMinSeekTime);
	Q_PROPERTY(qreal maxSeekTime READ default NOTIFY timelineChanged BINDABLE bindableMaxSeekTime);
	/// The position in the track in seconds.
	///
	/// Apps only report the position now and then, so while playing this is estimated from the
	/// last report and @@playbackRate. Like @@Quickshell.Services.Mpris.MprisPlayer.position,
	/// it is only notified when the app reports a position: emit `positionChanged` yourself while
	/// you show it.
	///
	/// May only be set if @@playbackPositionEnabled is true.
	Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged);
	// clang-format on

public:
	explicit MediaSession(MediaSessionPrivate* impl, QObject* parent);
	~MediaSession() override;
	Q_DISABLE_COPY_MOVE(MediaSession);

	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void togglePlayPause();
	Q_INVOKABLE void skipNext();
	Q_INVOKABLE void skipPrevious();
	Q_INVOKABLE void fastForward();
	Q_INVOKABLE void rewind();
	/// Restores the app's top window if minimized and brings it to the foreground.
	///
	/// The window is found by its AppUserModelID, or else by the program's file name.
	/// Returns false if there is none, or if Windows refused (see @@Hwnd.activate()).
	Q_INVOKABLE bool activate();

	[[nodiscard]] QString sourceAppUserModelId() const { return this->mSourceAppUserModelId; }
	[[nodiscard]] QString appName() const { return this->mAppName; }

	[[nodiscard]] QBindable<QString> bindableTitle() const { return &this->bTitle; }
	[[nodiscard]] QBindable<QString> bindableSubtitle() const { return &this->bSubtitle; }
	[[nodiscard]] QBindable<QString> bindableArtist() const { return &this->bArtist; }
	[[nodiscard]] QBindable<QString> bindableAlbumTitle() const { return &this->bAlbumTitle; }
	[[nodiscard]] QBindable<QString> bindableAlbumArtist() const { return &this->bAlbumArtist; }
	[[nodiscard]] QBindable<qint32> bindableTrackNumber() const { return &this->bTrackNumber; }
	[[nodiscard]] QBindable<qint32> bindableAlbumTrackCount() const {
		return &this->bAlbumTrackCount;
	}
	[[nodiscard]] QBindable<QList<QString>> bindableGenres() const { return &this->bGenres; }
	[[nodiscard]] QBindable<QString> bindableThumbnailUrl() const { return &this->bThumbnailUrl; }

	[[nodiscard]] QBindable<MediaPlaybackStatus::Enum> bindablePlaybackStatus() const {
		return &this->bPlaybackStatus;
	}
	[[nodiscard]] QBindable<MediaPlaybackType::Enum> bindablePlaybackType() const {
		return &this->bPlaybackType;
	}
	[[nodiscard]] QBindable<bool> bindablePlayEnabled() const { return &this->bPlayEnabled; }
	[[nodiscard]] QBindable<bool> bindablePauseEnabled() const { return &this->bPauseEnabled; }
	[[nodiscard]] QBindable<bool> bindableStopEnabled() const { return &this->bStopEnabled; }
	[[nodiscard]] QBindable<bool> bindableNextEnabled() const { return &this->bNextEnabled; }
	[[nodiscard]] QBindable<bool> bindablePreviousEnabled() const { return &this->bPreviousEnabled; }
	[[nodiscard]] QBindable<bool> bindableFastForwardEnabled() const {
		return &this->bFastForwardEnabled;
	}
	[[nodiscard]] QBindable<bool> bindableRewindEnabled() const { return &this->bRewindEnabled; }
	[[nodiscard]] QBindable<bool> bindablePlaybackPositionEnabled() const {
		return &this->bPlaybackPositionEnabled;
	}
	[[nodiscard]] QBindable<bool> bindablePlaybackRateEnabled() const {
		return &this->bPlaybackRateEnabled;
	}
	[[nodiscard]] QBindable<bool> bindableRepeatEnabled() const { return &this->bRepeatEnabled; }
	[[nodiscard]] QBindable<bool> bindableShuffleEnabled() const { return &this->bShuffleEnabled; }
	[[nodiscard]] QBindable<MediaAutoRepeatMode::Enum> bindableAutoRepeatMode() const {
		return &this->bAutoRepeatMode;
	}
	[[nodiscard]] QBindable<bool> bindableShuffleActive() const { return &this->bShuffleActive; }
	[[nodiscard]] QBindable<qreal> bindablePlaybackRate() const { return &this->bPlaybackRate; }

	[[nodiscard]] QBindable<qreal> bindableStartTime() const { return &this->bStartTime; }
	[[nodiscard]] QBindable<qreal> bindableEndTime() const { return &this->bEndTime; }
	[[nodiscard]] QBindable<qreal> bindableMinSeekTime() const { return &this->bMinSeekTime; }
	[[nodiscard]] QBindable<qreal> bindableMaxSeekTime() const { return &this->bMaxSeekTime; }

	void setAutoRepeatMode(MediaAutoRepeatMode::Enum mode);
	void setShuffleActive(bool shuffle);
	void setPlaybackRate(qreal rate);

	[[nodiscard]] qreal position() const;
	void setPosition(qreal position);

signals:
	/// Track info changed. All of it has been updated when this is sent.
	void mediaPropertiesChanged();

	void titleChanged();
	void subtitleChanged();
	void artistChanged();
	void albumTitleChanged();
	void albumArtistChanged();
	void trackNumberChanged();
	void albumTrackCountChanged();
	void genresChanged();
	void thumbnailUrlChanged();
	void playbackStatusChanged();
	void playbackTypeChanged();
	void playEnabledChanged();
	void pauseEnabledChanged();
	void stopEnabledChanged();
	void nextEnabledChanged();
	void previousEnabledChanged();
	void fastForwardEnabledChanged();
	void rewindEnabledChanged();
	void playbackPositionEnabledChanged();
	void playbackRateEnabledChanged();
	void repeatEnabledChanged();
	void shuffleEnabledChanged();
	void autoRepeatModeChanged();
	void shuffleActiveChanged();
	void playbackRateChanged();
	void timelineChanged();
	void positionChanged();

private:
	void refreshPlaybackInfo();
	void refreshTimeline();
	void requestMediaProperties();
	void applyMediaProperties(quint64 generation, const MediaPropertiesData& data);

	MediaSessionPrivate* impl;
	QString mSourceAppUserModelId;
	QString mAppName;
	MediaThumbnail thumbnail;
	quint64 mediaGeneration = 0;

	// Last reported position, in seconds, and when it was reported.
	qreal mPosition = 0;
	QDateTime mPositionTime;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QString, bTitle, &MediaSession::titleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QString, bSubtitle, &MediaSession::subtitleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QString, bArtist, &MediaSession::artistChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QString, bAlbumTitle, &MediaSession::albumTitleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QString, bAlbumArtist, &MediaSession::albumArtistChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, qint32, bTrackNumber, &MediaSession::trackNumberChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, qint32, bAlbumTrackCount, &MediaSession::albumTrackCountChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QList<QString>, bGenres, &MediaSession::genresChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, QString, bThumbnailUrl, &MediaSession::thumbnailUrlChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, MediaPlaybackStatus::Enum, bPlaybackStatus, &MediaSession::playbackStatusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, MediaPlaybackType::Enum, bPlaybackType, &MediaSession::playbackTypeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bPlayEnabled, &MediaSession::playEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bPauseEnabled, &MediaSession::pauseEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bStopEnabled, &MediaSession::stopEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bNextEnabled, &MediaSession::nextEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bPreviousEnabled, &MediaSession::previousEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bFastForwardEnabled, &MediaSession::fastForwardEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bRewindEnabled, &MediaSession::rewindEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bPlaybackPositionEnabled, &MediaSession::playbackPositionEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bPlaybackRateEnabled, &MediaSession::playbackRateEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bRepeatEnabled, &MediaSession::repeatEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bShuffleEnabled, &MediaSession::shuffleEnabledChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, MediaAutoRepeatMode::Enum, bAutoRepeatMode, &MediaSession::autoRepeatModeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, bool, bShuffleActive, &MediaSession::shuffleActiveChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MediaSession, qreal, bPlaybackRate, 1, &MediaSession::playbackRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, qreal, bStartTime, &MediaSession::timelineChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, qreal, bEndTime, &MediaSession::timelineChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, qreal, bMinSeekTime, &MediaSession::timelineChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MediaSession, qreal, bMaxSeekTime, &MediaSession::timelineChanged);
	// clang-format on

	friend class MediaSessionPrivate;
	friend class MediaService;
};

class MediaServicePrivate;

// Process wide session list behind MediaSessions, so it is only watched once across reloads.
class MediaService: public QObject {
	Q_OBJECT;

public:
	static MediaService* instance();

	[[nodiscard]] ObjectModel<MediaSession>* sessions() { return &this->mSessions; }
	[[nodiscard]] MediaSession* currentSession() const { return this->mCurrentSession; }

signals:
	void currentSessionChanged();

private:
	explicit MediaService();

	void refreshSessions();
	void refreshCurrentSession();

	MediaServicePrivate* impl;
	ObjectModel<MediaSession> mSessions {this};
	MediaSession* mCurrentSession = nullptr;

	friend class MediaServicePrivate;
};

///! Media sessions of all apps.
/// The apps Windows shows in its media flyout, through [GlobalSystemMediaTransportControlsSessionManager].
/// @@Quickshell.Services.Mpris is provided on top of this for configs shared with Linux.
///
/// [GlobalSystemMediaTransportControlsSessionManager]: https://learn.microsoft.com/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager
class MediaSessionsQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(MediaSessions);
	QML_SINGLETON;
	// clang-format off
	/// Each app's media session.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::MediaSession>*);
	Q_PROPERTY(UntypedObjectModel* sessions READ sessions CONSTANT);
	/// The session Windows considers current, which media keys control. May be null.
	Q_PROPERTY(qs::windows::MediaSession* currentSession READ currentSession NOTIFY currentSessionChanged);
	// clang-format on

public:
	explicit MediaSessionsQml(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<MediaSession>* sessions() {
		return MediaService::instance()->sessions();
	}

	[[nodiscard]] static MediaSession* currentSession() {
		return MediaService::instance()->currentSession();
	}

signals:
	void currentSessionChanged();
};

} // namespace qs::windows
