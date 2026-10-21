#include "qml.hpp"

#include <qnamespace.h>
#include <qobject.h>

#include "../../core/model.hpp"
#include "../notify_icons.hpp"
#include "item.hpp"

namespace qs::windows::systray {

SystemTrayWatcher::SystemTrayWatcher() {
	auto* icons = NotifyIconService::instance()->icons();

	QObject::connect(
	    icons,
	    &UntypedObjectModel::objectInsertedPost,
	    this,
	    &SystemTrayWatcher::onIconInserted
	);

	QObject::connect(
	    icons,
	    &UntypedObjectModel::objectRemovedPre,
	    this,
	    &SystemTrayWatcher::onIconRemoved
	);

	for (auto* icon: icons->valueList()) {
		this->onIconInserted(icon);
	}
}

void SystemTrayWatcher::onIconInserted(QObject* object) {
	auto* icon = static_cast<NotifyIcon*>(object); // NOLINT
	auto* item = new StatusNotifierItem(icon, this);
	this->mItemMap.insert(icon, item);
	this->mItems.insertObjectSorted(item, &SystemTrayWatcher::compareItems);
}

void SystemTrayWatcher::onIconRemoved(QObject* object) {
	auto* item = this->mItemMap.take(static_cast<NotifyIcon*>(object)); // NOLINT
	if (!item) return;

	this->mItems.removeObject(item);
	item->deleteLater();
}

// Same order as upstream's SystemTray::compareItems.
bool SystemTrayWatcher::compareItems(StatusNotifierItem* a, StatusNotifierItem* b) {
	return a->category() < b->category() || a->id().compare(b->id(), Qt::CaseInsensitive) >= 0;
}

SystemTrayWatcher* SystemTrayWatcher::instance() {
	static auto* instance = new SystemTrayWatcher(); // NOLINT
	return instance;
}

} // namespace qs::windows::systray
