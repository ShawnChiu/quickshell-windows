#pragma once

#include <qhash.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"
#include "../notify_icons.hpp"
#include "item.hpp"

namespace qs::windows::systray {

class SystemTrayWatcher: public QObject {
	Q_OBJECT;

public:
	[[nodiscard]] ObjectModel<StatusNotifierItem>* items() { return &this->mItems; }

	static SystemTrayWatcher* instance();

private slots:
	void onIconInserted(QObject* object);
	void onIconRemoved(QObject* object);

private:
	explicit SystemTrayWatcher();

	static bool compareItems(StatusNotifierItem* a, StatusNotifierItem* b);

	QHash<NotifyIcon*, StatusNotifierItem*> mItemMap;
	ObjectModel<StatusNotifierItem> mItems {this};
};

///! System tray
/// Referencing the SystemTray singleton will make quickshell start tracking
/// system tray contents, which are updated as the tray changes, and can be
/// accessed via the @@items property.
///
/// On Windows, the icons in the notification area, see @@Quickshell.Windows.NotifyIcons.
class SystemTray: public QObject {
	Q_OBJECT;
	/// List of all system tray icons.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::systray::StatusNotifierItem>*);
	Q_PROPERTY(UntypedObjectModel* items READ items CONSTANT);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit SystemTray(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static ObjectModel<StatusNotifierItem>* items() {
		return SystemTrayWatcher::instance()->items();
	}
};

} // namespace qs::windows::systray
