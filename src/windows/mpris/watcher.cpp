#include "watcher.hpp"

#include <qobject.h>

#include "../../core/model.hpp"
#include "../media.hpp"
#include "player.hpp"

namespace qs::windows::mpris {

MprisWatcher::MprisWatcher() {
	auto* sessions = MediaService::instance()->sessions();

	QObject::connect(
	    sessions,
	    &UntypedObjectModel::objectInsertedPost,
	    this,
	    &MprisWatcher::onSessionInserted
	);

	QObject::connect(
	    sessions,
	    &UntypedObjectModel::objectRemovedPre,
	    this,
	    &MprisWatcher::onSessionRemoved
	);

	for (auto* session: sessions->valueList()) {
		this->onSessionInserted(session);
	}
}

void MprisWatcher::onSessionInserted(QObject* object) {
	auto* session = static_cast<MediaSession*>(object); // NOLINT
	auto* player = new MprisPlayer(session, this);
	this->mPlayerMap.insert(session, player);
	this->mPlayers.insertObject(player);
}

void MprisWatcher::onSessionRemoved(QObject* object) {
	auto* player = this->mPlayerMap.take(static_cast<MediaSession*>(object)); // NOLINT
	if (!player) return;

	this->mPlayers.removeObject(player);
	player->deleteLater();
}

MprisWatcher* MprisWatcher::instance() {
	static auto* instance = new MprisWatcher(); // NOLINT
	return instance;
}

} // namespace qs::windows::mpris
