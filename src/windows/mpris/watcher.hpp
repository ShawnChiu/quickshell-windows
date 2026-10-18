#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "../media.hpp"
#include "player.hpp"

namespace qs::windows::mpris {

class MprisWatcher: public QObject {
	Q_OBJECT;

public:
	[[nodiscard]] ObjectModel<MprisPlayer>* players() { return &this->mPlayers; }

	static MprisWatcher* instance();

private slots:
	void onSessionInserted(QObject* object);
	void onSessionRemoved(QObject* object);

private:
	explicit MprisWatcher();

	QHash<MediaSession*, MprisPlayer*> mPlayerMap;
	ObjectModel<MprisPlayer> mPlayers {this};
};

///! Provides access to MprisPlayers.
/// On Windows, each app's media session, see @@Quickshell.Windows.MediaSessions.
class MprisQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Mpris);
	QML_SINGLETON;
	/// All connected MPRIS players.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::mpris::MprisPlayer>*);
	Q_PROPERTY(UntypedObjectModel* players READ players CONSTANT);

public:
	explicit MprisQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static ObjectModel<MprisPlayer>* players() {
		return MprisWatcher::instance()->players();
	}
};

} // namespace qs::windows::mpris
