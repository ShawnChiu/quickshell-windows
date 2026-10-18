#pragma once

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../media.hpp"

// Quickshell.Services.Mpris on top of Quickshell.Windows.MediaSessions, so configs shared
// with Linux run unchanged. The QML API mirrors src/services/mpris; diff it after syncs.
namespace qs::windows::mpris {

///! Playback state of an MprisPlayer
/// See @@MprisPlayer.playbackState.
class MprisPlaybackState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Stopped = 0,
		Playing = 1,
		Paused = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::mpris::MprisPlaybackState::Enum status);
};

///! Loop state of an MprisPlayer
/// See @@MprisPlayer.loopState.
class MprisLoopState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		Track = 1,
		Playlist = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::mpris::MprisLoopState::Enum status);
};

///! A media session, as an MPRIS player.
/// On Windows, an app's media session as shown in Windows' media flyout,
/// see @@Quickshell.Windows.MediaSession.
///
/// Every player can be controlled and raised. None can quit, go fullscreen, open uris or
/// report its volume. Windows does not report a playback rate range, so @@minRate and
/// @@maxRate are 0.25 and 4 when the app accepts rate changes, otherwise 1.
class MprisPlayer: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(bool canControl READ default NOTIFY canControlChanged BINDABLE bindableCanControl);
	Q_PROPERTY(bool canPlay READ default NOTIFY canPlayChanged BINDABLE bindableCanPlay);
	Q_PROPERTY(bool canPause READ default NOTIFY canPauseChanged BINDABLE bindableCanPause);
	Q_PROPERTY(bool canTogglePlaying READ default NOTIFY canTogglePlayingChanged BINDABLE bindableCanTogglePlaying);
	Q_PROPERTY(bool canSeek READ default NOTIFY canSeekChanged BINDABLE bindableCanSeek);
	Q_PROPERTY(bool canGoNext READ default NOTIFY canGoNextChanged BINDABLE bindableCanGoNext);
	Q_PROPERTY(bool canGoPrevious READ default NOTIFY canGoPreviousChanged BINDABLE bindableCanGoPrevious);
	Q_PROPERTY(bool canQuit READ default NOTIFY canQuitChanged BINDABLE bindableCanQuit);
	Q_PROPERTY(bool canRaise READ default NOTIFY canRaiseChanged BINDABLE bindableCanRaise);
	Q_PROPERTY(bool canSetFullscreen READ default NOTIFY canSetFullscreenChanged BINDABLE bindableCanSetFullscreen);
	/// The app's display name.
	Q_PROPERTY(QString identity READ default NOTIFY identityChanged BINDABLE bindableIdentity);
	/// The app's AppUserModelID without `.exe`, which finds Windows' desktop entry for most programs.
	Q_PROPERTY(QString desktopEntry READ default NOTIFY desktopEntryChanged BINDABLE bindableDesktopEntry);
	/// The app's AppUserModelID.
	Q_PROPERTY(QString dbusName READ address CONSTANT);
	/// The current position in the playing track, as seconds, or `0` if @@positionSupported is false.
	///
	/// May only be written to if @@canSeek and @@positionSupported are true.
	///
	/// As on Linux, `position` is only notified when the app reports a position:
	/// emit @@positionChanged yourself while you show it.
	Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged);
	/// True if the app reports a timeline.
	Q_PROPERTY(bool positionSupported READ positionSupported NOTIFY positionSupportedChanged);
	/// The length of the playing track, as seconds, or the value of @@position if
	/// @@lengthSupported is false.
	Q_PROPERTY(qreal length READ length NOTIFY lengthChanged);
	Q_PROPERTY(bool lengthSupported READ default NOTIFY lengthSupportedChanged BINDABLE bindableLengthSupported);
	/// Always 1, since Windows does not report app volumes through media sessions.
	Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged);
	/// Always false.
	Q_PROPERTY(bool volumeSupported READ volumeSupported NOTIFY volumeSupportedChanged);
	/// Metadata of the current track, with the MPRIS keys Windows has a counterpart for:
	/// `xesam:title`, `xesam:artist`, `xesam:album`, `xesam:albumArtist`, `xesam:genre`,
	/// `xesam:trackNumber`, `mpris:artUrl` and `mpris:length`.
	Q_PROPERTY(QVariantMap metadata READ default NOTIFY metadataChanged BINDABLE bindableMetadata);
	/// An opaque identifier for the current track unique within the current player.
	Q_PROPERTY(quint32 uniqueId READ default NOTIFY uniqueIdChanged BINDABLE bindableUniqueId);
	Q_PROPERTY(QString trackTitle READ default NOTIFY trackTitleChanged BINDABLE bindableTrackTitle);
	Q_PROPERTY(QString trackArtist READ default NOTIFY trackArtistChanged BINDABLE bindableTrackArtist);
	/// > [!ERROR] deprecated in favor of @@trackArtist.
	Q_PROPERTY(QString trackArtists READ default NOTIFY trackArtistChanged BINDABLE bindableTrackArtist);
	Q_PROPERTY(QString trackAlbum READ default NOTIFY trackAlbumChanged BINDABLE bindableTrackAlbum);
	Q_PROPERTY(QString trackAlbumArtist READ default NOTIFY trackAlbumArtistChanged BINDABLE bindableTrackAlbumArtist);
	/// The track's thumbnail as an image url, or `""` if none was provided.
	Q_PROPERTY(QString trackArtUrl READ default NOTIFY trackArtUrlChanged BINDABLE bindableTrackArtUrl);
	/// The playback state of the media player.
	///
	/// Windows' `Changing` state (between tracks) keeps the previous state,
	/// and `Closed` and `Opened` are `Stopped`.
	Q_PROPERTY(qs::windows::mpris::MprisPlaybackState::Enum playbackState READ playbackState WRITE setPlaybackState NOTIFY playbackStateChanged);
	Q_PROPERTY(bool isPlaying READ isPlaying WRITE setPlaying NOTIFY isPlayingChanged);
	Q_PROPERTY(qs::windows::mpris::MprisLoopState::Enum loopState READ loopState WRITE setLoopState NOTIFY loopStateChanged);
	Q_PROPERTY(bool loopSupported READ loopSupported NOTIFY loopSupportedChanged);
	Q_PROPERTY(qreal rate READ rate WRITE setRate NOTIFY rateChanged);
	Q_PROPERTY(qreal minRate READ default NOTIFY minRateChanged BINDABLE bindableMinRate);
	Q_PROPERTY(qreal maxRate READ default NOTIFY maxRateChanged BINDABLE bindableMaxRate);
	Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged);
	Q_PROPERTY(bool shuffleSupported READ shuffleSupported NOTIFY shuffleSupportedChanged);
	/// Always false.
	Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen NOTIFY fullscreenChanged);
	/// Always empty.
	Q_PROPERTY(QList<QString> supportedUriSchemes READ default NOTIFY supportedUriSchemesChanged BINDABLE bindableSupportedUriSchemes);
	/// Always empty.
	Q_PROPERTY(QList<QString> supportedMimeTypes READ default NOTIFY supportedMimeTypesChanged BINDABLE bindableSupportedMimeTypes);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("MprisPlayers can only be acquired from Mpris");

public:
	explicit MprisPlayer(MediaSession* session, QObject* parent = nullptr);

	/// Bring the app's window to the front, see @@Quickshell.Windows.MediaSession.activate().
	Q_INVOKABLE void raise();
	/// Not supported on Windows.
	Q_INVOKABLE void quit();
	/// Not supported on Windows.
	Q_INVOKABLE void openUri(const QString& uri);
	Q_INVOKABLE void next();
	Q_INVOKABLE void previous();
	Q_INVOKABLE void seek(qreal offset);
	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void togglePlaying();

	[[nodiscard]] QString address() const { return this->session->sourceAppUserModelId(); }

	[[nodiscard]] QBindable<bool> bindableCanControl() const { return &this->bCanControl; }
	[[nodiscard]] QBindable<bool> bindableCanSeek() const { return &this->bCanSeek; }
	[[nodiscard]] QBindable<bool> bindableCanGoNext() const { return &this->bCanGoNext; }
	[[nodiscard]] QBindable<bool> bindableCanGoPrevious() const { return &this->bCanGoPrevious; }
	[[nodiscard]] QBindable<bool> bindableCanPlay() const { return &this->bCanPlay; }
	[[nodiscard]] QBindable<bool> bindableCanPause() const { return &this->bCanPause; }
	[[nodiscard]] QBindable<bool> bindableCanTogglePlaying() const {
		return &this->bCanTogglePlaying;
	}
	[[nodiscard]] QBindable<bool> bindableCanQuit() const { return &this->bCanQuit; }
	[[nodiscard]] QBindable<bool> bindableCanRaise() const { return &this->bCanRaise; }
	[[nodiscard]] QBindable<bool> bindableCanSetFullscreen() const {
		return &this->bCanSetFullscreen;
	}

	[[nodiscard]] QBindable<QString> bindableIdentity() const { return &this->bIdentity; }
	[[nodiscard]] QBindable<QString> bindableDesktopEntry() const { return &this->bDesktopEntry; }

	[[nodiscard]] qreal position() const;
	[[nodiscard]] bool positionSupported() const { return this->bLengthSupported; }
	void setPosition(qreal position);

	[[nodiscard]] qreal length() const;
	[[nodiscard]] QBindable<bool> bindableLengthSupported() const { return &this->bLengthSupported; }

	[[nodiscard]] static qreal volume() { return 1; }
	[[nodiscard]] static bool volumeSupported() { return false; }
	void setVolume(qreal volume);

	[[nodiscard]] QBindable<quint32> bindableUniqueId() const { return &this->bUniqueId; }
	[[nodiscard]] QBindable<QVariantMap> bindableMetadata() const { return &this->bMetadata; }
	[[nodiscard]] QBindable<QString> bindableTrackTitle() const { return &this->bTrackTitle; }
	[[nodiscard]] QBindable<QString> bindableTrackAlbum() const { return &this->bTrackAlbum; }
	[[nodiscard]] QBindable<QString> bindableTrackAlbumArtist() const {
		return &this->bTrackAlbumArtist;
	}
	[[nodiscard]] QBindable<QString> bindableTrackArtist() const { return &this->bTrackArtist; }
	[[nodiscard]] QBindable<QString> bindableTrackArtUrl() const { return &this->bTrackArtUrl; }

	[[nodiscard]] MprisPlaybackState::Enum playbackState() const { return this->bPlaybackState; }
	void setPlaybackState(MprisPlaybackState::Enum playbackState);

	[[nodiscard]] bool isPlaying() const { return this->bIsPlaying; }
	void setPlaying(bool playing);

	[[nodiscard]] MprisLoopState::Enum loopState() const { return this->bLoopState; }
	[[nodiscard]] bool loopSupported() const { return this->bLoopSupported; }
	void setLoopState(MprisLoopState::Enum loopState);

	[[nodiscard]] qreal rate() const { return this->bRate; }
	[[nodiscard]] QBindable<qreal> bindableMinRate() const { return &this->bMinRate; }
	[[nodiscard]] QBindable<qreal> bindableMaxRate() const { return &this->bMaxRate; }
	void setRate(qreal rate);

	[[nodiscard]] bool shuffle() const { return this->bShuffle; }
	[[nodiscard]] bool shuffleSupported() const { return this->bShuffleSupported; }
	void setShuffle(bool shuffle);

	[[nodiscard]] static bool fullscreen() { return false; }
	void setFullscreen(bool fullscreen);

	[[nodiscard]] QBindable<QList<QString>> bindableSupportedUriSchemes() const {
		return &this->bSupportedUriSchemes;
	}

	[[nodiscard]] QBindable<QList<QString>> bindableSupportedMimeTypes() const {
		return &this->bSupportedMimeTypes;
	}

signals:
	/// The track has changed.
	///
	/// All track information properties will be updated immediately following this signal.
	/// @@postTrackChanged will be sent after they update.
	void trackChanged();
	/// Sent after track info related properties have been updated, following @@trackChanged.
	void postTrackChanged();

	QSDOC_HIDE void ready();
	void canControlChanged();
	void canPlayChanged();
	void canPauseChanged();
	void canTogglePlayingChanged();
	void canSeekChanged();
	void canGoNextChanged();
	void canGoPreviousChanged();
	void canQuitChanged();
	void canRaiseChanged();
	void canSetFullscreenChanged();
	void identityChanged();
	void desktopEntryChanged();
	void positionChanged();
	void positionSupportedChanged();
	void lengthChanged();
	void lengthSupportedChanged();
	void volumeChanged();
	void volumeSupportedChanged();
	void metadataChanged();
	void uniqueIdChanged();
	void trackTitleChanged();
	void trackArtistChanged();
	void trackAlbumChanged();
	void trackAlbumArtistChanged();
	void trackArtUrlChanged();
	void playbackStateChanged();
	void isPlayingChanged();
	void loopStateChanged();
	void loopSupportedChanged();
	void rateChanged();
	void minRateChanged();
	void maxRateChanged();
	void shuffleChanged();
	void shuffleSupportedChanged();
	void fullscreenChanged();
	void supportedUriSchemesChanged();
	void supportedMimeTypesChanged();

private:
	void onMediaPropertiesChanged();
	void onPlaybackStatusChanged();
	void onPositionChanged();

	MediaSession* session;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bIdentity, &MprisPlayer::identityChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bDesktopEntry, &MprisPlayer::desktopEntryChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanQuit, &MprisPlayer::canQuitChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, bool, bCanRaise, true, &MprisPlayer::canRaiseChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanSetFullscreen, &MprisPlayer::canSetFullscreenChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QList<QString>, bSupportedUriSchemes, &MprisPlayer::supportedUriSchemesChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QList<QString>, bSupportedMimeTypes, &MprisPlayer::supportedMimeTypesChanged);

	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, bool, bCanControl, true, &MprisPlayer::canControlChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanPlay, &MprisPlayer::canPlayChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanPause, &MprisPlayer::canPauseChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanTogglePlaying, &MprisPlayer::canTogglePlayingChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanSeek, &MprisPlayer::canSeekChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanGoNext, &MprisPlayer::canGoNextChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bCanGoPrevious, &MprisPlayer::canGoPreviousChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, MprisPlaybackState::Enum, bPlaybackState, &MprisPlayer::playbackStateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bIsPlaying, &MprisPlayer::isPlayingChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, MprisLoopState::Enum, bLoopState, &MprisPlayer::loopStateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bLoopSupported, &MprisPlayer::loopSupportedChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, qreal, bRate, 1, &MprisPlayer::rateChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, qreal, bMinRate, 1, &MprisPlayer::minRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(MprisPlayer, qreal, bMaxRate, 1, &MprisPlayer::maxRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bShuffle, &MprisPlayer::shuffleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bShuffleSupported, &MprisPlayer::shuffleSupportedChanged);

	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QVariantMap, bMetadata, &MprisPlayer::metadataChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, quint32, bUniqueId, &MprisPlayer::uniqueIdChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackTitle, &MprisPlayer::trackTitleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackArtist, &MprisPlayer::trackArtistChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackAlbum, &MprisPlayer::trackAlbumChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackAlbumArtist, &MprisPlayer::trackAlbumArtistChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, QString, bTrackArtUrl, &MprisPlayer::trackArtUrlChanged);
	Q_OBJECT_BINDABLE_PROPERTY(MprisPlayer, bool, bLengthSupported, &MprisPlayer::lengthSupportedChanged);
	// clang-format on
};

} // namespace qs::windows::mpris
