#include "player.hpp"

#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qobject.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtypes.h>

#include "../media.hpp"

namespace qs::windows::mpris {

QString MprisPlaybackState::toString(MprisPlaybackState::Enum status) {
	switch (status) {
	case MprisPlaybackState::Stopped: return "Stopped";
	case MprisPlaybackState::Playing: return "Playing";
	case MprisPlaybackState::Paused: return "Paused";
	default: return "Unknown Status";
	}
}

QString MprisLoopState::toString(MprisLoopState::Enum status) {
	switch (status) {
	case MprisLoopState::None: return "None";
	case MprisLoopState::Track: return "Track";
	case MprisLoopState::Playlist: return "Playlist";
	default: return "Unknown Status";
	}
}

MprisPlayer::MprisPlayer(MediaSession* session, QObject* parent)
    : QObject(parent)
    , session(session) {
	auto aumid = session->sourceAppUserModelId();
	this->bIdentity = session->appName();
	this->bDesktopEntry = aumid.endsWith(".exe", Qt::CaseInsensitive) ? aumid.chopped(4) : aumid;

	// clang-format off
	this->bCanPlay.setBinding([session]() { return session->bindablePlayEnabled().value(); });
	this->bCanPause.setBinding([session]() { return session->bindablePauseEnabled().value(); });
	this->bCanSeek.setBinding([session]() { return session->bindablePlaybackPositionEnabled().value(); });
	this->bCanGoNext.setBinding([session]() { return session->bindableNextEnabled().value(); });
	this->bCanGoPrevious.setBinding([session]() { return session->bindablePreviousEnabled().value(); });
	this->bLoopSupported.setBinding([session]() { return session->bindableRepeatEnabled().value(); });
	this->bShuffleSupported.setBinding([session]() { return session->bindableShuffleEnabled().value(); });
	this->bShuffle.setBinding([session]() { return session->bindableShuffleActive().value(); });
	this->bRate.setBinding([session]() { return session->bindablePlaybackRate().value(); });
	// clang-format on

	this->bCanTogglePlaying.setBinding([this]() {
		return this->bIsPlaying ? this->bCanPause.value() : this->bCanPlay.value();
	});

	this->bIsPlaying.setBinding([this]() {
		return this->bPlaybackState == MprisPlaybackState::Playing;
	});

	// Same values.
	this->bLoopState.setBinding([session]() {
		return static_cast<MprisLoopState::Enum>(session->bindableAutoRepeatMode().value());
	});

	this->bMinRate.setBinding([session]() {
		return session->bindablePlaybackRateEnabled().value() ? 0.25 : 1.0;
	});

	this->bMaxRate.setBinding([session]() {
		return session->bindablePlaybackRateEnabled().value() ? 4.0 : 1.0;
	});

	this->bLengthSupported.setBinding([session]() {
		return session->bindableEndTime().value() > session->bindableStartTime().value();
	});

	this->bMetadata.setBinding([this, session]() {
		auto metadata = QVariantMap();
		auto put = [&metadata](const QString& key, const QString& value) {
			if (!value.isEmpty()) metadata.insert(key, value);
		};

		put("xesam:title", this->bTrackTitle);
		put("xesam:album", this->bTrackAlbum);
		put("mpris:artUrl", this->bTrackArtUrl);

		if (!this->bTrackArtist.value().isEmpty()) {
			metadata.insert("xesam:artist", QList<QString> {this->bTrackArtist});
		}

		if (!this->bTrackAlbumArtist.value().isEmpty()) {
			metadata.insert("xesam:albumArtist", QList<QString> {this->bTrackAlbumArtist});
		}

		auto genres = session->bindableGenres().value();
		if (!genres.isEmpty()) metadata.insert("xesam:genre", genres);

		auto trackNumber = session->bindableTrackNumber().value();
		if (trackNumber > 0) metadata.insert("xesam:trackNumber", trackNumber);

		if (this->bLengthSupported) {
			metadata.insert("mpris:length", static_cast<qlonglong>(this->length() * 1000000));
		}

		return metadata;
	});

	// clang-format off
	QObject::connect(session, &MediaSession::mediaPropertiesChanged, this, &MprisPlayer::onMediaPropertiesChanged);
	QObject::connect(session, &MediaSession::playbackStatusChanged, this, &MprisPlayer::onPlaybackStatusChanged);
	QObject::connect(session, &MediaSession::positionChanged, this, &MprisPlayer::positionChanged);
	QObject::connect(session, &MediaSession::timelineChanged, this, &MprisPlayer::lengthChanged);
	QObject::connect(this, &MprisPlayer::lengthSupportedChanged, this, &MprisPlayer::positionSupportedChanged);

	// Ensure user triggered position updates can update length.
	QObject::connect(this, &MprisPlayer::positionChanged, this, &MprisPlayer::onPositionChanged);
	// clang-format on

	this->onMediaPropertiesChanged();
	this->onPlaybackStatusChanged();
}

void MprisPlayer::onMediaPropertiesChanged() {
	auto title = this->session->bindableTitle().value();
	auto artist = this->session->bindableArtist().value();
	auto album = this->session->bindableAlbumTitle().value();

	auto trackChanged =
	    title != this->bTrackTitle || artist != this->bTrackArtist || album != this->bTrackAlbum;

	if (trackChanged) emit this->trackChanged();

	Qt::beginPropertyUpdateGroup();
	if (trackChanged) this->bUniqueId = this->bUniqueId + 1;
	this->bTrackTitle = title;
	this->bTrackArtist = artist;
	this->bTrackAlbum = album;
	this->bTrackAlbumArtist = this->session->bindableAlbumArtist().value();
	this->bTrackArtUrl = this->session->bindableThumbnailUrl().value();
	Qt::endPropertyUpdateGroup();

	if (trackChanged) emit this->postTrackChanged();
}

void MprisPlayer::onPlaybackStatusChanged() {
	switch (this->session->bindablePlaybackStatus().value()) {
	case MediaPlaybackStatus::Playing: this->bPlaybackState = MprisPlaybackState::Playing; break;
	case MediaPlaybackStatus::Paused: this->bPlaybackState = MprisPlaybackState::Paused; break;
	// Between tracks.
	case MediaPlaybackStatus::Changing: break;
	default: this->bPlaybackState = MprisPlaybackState::Stopped; break;
	}
}

void MprisPlayer::onPositionChanged() {
	if (!this->bLengthSupported) emit this->lengthChanged();
}

void MprisPlayer::raise() { this->session->activate(); }

void MprisPlayer::quit() {
	qWarning() << "Cannot call quit() on" << this << "because canQuit is false.";
}

void MprisPlayer::openUri(const QString& /*uri*/) {
	qWarning() << "Cannot call openUri() on" << this << "because Windows media sessions cannot.";
}

void MprisPlayer::next() {
	if (!this->bCanGoNext) {
		qWarning() << "Cannot call next() on" << this << "because canGoNext is false.";
		return;
	}

	this->session->skipNext();
}

void MprisPlayer::previous() {
	if (!this->bCanGoPrevious) {
		qWarning() << "Cannot call previous() on" << this << "because canGoPrevious is false.";
		return;
	}

	this->session->skipPrevious();
}

void MprisPlayer::seek(qreal offset) {
	if (!this->bCanSeek) {
		qWarning() << "Cannot call seek() on" << this << "because canSeek is false.";
		return;
	}

	this->session->setPosition(this->session->position() + offset);
}

qreal MprisPlayer::position() const {
	if (!this->positionSupported()) return 0; // unsupported
	if (this->bPlaybackState == MprisPlaybackState::Stopped) return 0;

	return this->session->position() - this->session->bindableStartTime().value();
}

void MprisPlayer::setPosition(qreal position) {
	if (!this->positionSupported()) {
		qWarning() << "Cannot set position of" << this << "because position is not supported.";
		return;
	}

	if (!this->bCanSeek) {
		qWarning() << "Cannot set position of" << this << "because canSeek is false.";
		return;
	}

	this->session->setPosition(this->session->bindableStartTime().value() + position);
}

qreal MprisPlayer::length() const {
	if (!this->bLengthSupported) return this->position(); // unsupported

	return this->session->bindableEndTime().value() - this->session->bindableStartTime().value();
}

void MprisPlayer::setVolume(qreal /*volume*/) {
	qWarning() << "Cannot set volume of" << this << "because volume is not supported.";
}

void MprisPlayer::setPlaybackState(MprisPlaybackState::Enum playbackState) {
	if (playbackState == this->bPlaybackState) return;

	switch (playbackState) {
	case MprisPlaybackState::Stopped: this->session->stop(); break;
	case MprisPlaybackState::Playing:
		if (!this->bCanPlay) {
			qWarning() << "Cannot set playbackState of" << this << "to Playing because canPlay is false.";
			return;
		}

		this->session->play();
		break;
	case MprisPlaybackState::Paused:
		if (!this->bCanPause) {
			qWarning() << "Cannot set playbackState of" << this << "to Paused because canPause is false.";
			return;
		}

		this->session->pause();
		break;
	default:
		qWarning() << "Cannot set playbackState of" << this << "to unknown value" << playbackState;
		return;
	}
}

void MprisPlayer::play() { this->setPlaybackState(MprisPlaybackState::Playing); }
void MprisPlayer::pause() { this->setPlaybackState(MprisPlaybackState::Paused); }
void MprisPlayer::stop() { this->setPlaybackState(MprisPlaybackState::Stopped); }

void MprisPlayer::togglePlaying() {
	if (this->bIsPlaying) {
		this->pause();
	} else {
		this->play();
	}
}

void MprisPlayer::setPlaying(bool playing) {
	if (playing == this->bIsPlaying) return;
	this->togglePlaying();
}

void MprisPlayer::setLoopState(MprisLoopState::Enum loopState) {
	if (!this->bLoopSupported) {
		qWarning() << "Cannot set loopState of" << this << "because loop state is not supported.";
		return;
	}

	if (loopState == this->bLoopState) return;
	if (loopState > MprisLoopState::Playlist) {
		qWarning() << "Cannot set loopState of" << this << "to unknown value" << loopState;
		return;
	}

	this->session->setAutoRepeatMode(static_cast<MediaAutoRepeatMode::Enum>(loopState));
}

void MprisPlayer::setRate(qreal rate) {
	if (rate == this->bRate.value()) return;

	if (rate < this->bMinRate.value() || rate > this->bMaxRate.value()) {
		qWarning() << "Cannot set rate for" << this << "to" << rate
		           << "which is outside of minRate and maxRate" << this->bMinRate.value()
		           << this->bMaxRate.value();
		return;
	}

	this->session->setPlaybackRate(rate);
}

void MprisPlayer::setShuffle(bool shuffle) {
	if (!this->bShuffleSupported) {
		qWarning() << "Cannot set shuffle for" << this << "because shuffle is not supported.";
		return;
	}

	this->session->setShuffleActive(shuffle);
}

void MprisPlayer::setFullscreen(bool /*fullscreen*/) {
	qWarning() << "Cannot set fullscreen for" << this << "because canSetFullscreen is false.";
}

} // namespace qs::windows::mpris
