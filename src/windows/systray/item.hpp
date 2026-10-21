#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../../core/qsmenu.hpp"
#include "../notify_icons.hpp"

// Quickshell.Services.SystemTray on top of Quickshell.Windows.NotifyIcons, so configs shared
// with Linux run unchanged. The QML API mirrors src/services/status_notifier; diff it after syncs.
namespace qs::windows::systray {

///! Status of a StatusNotifierItem.
/// See @@StatusNotifierItem.status.
namespace Status { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	/// A passive item does not convey important information and can be considered idle. You may want to hide these.
	Passive = 0,
	/// An active item may have information more important than a passive one and you probably do not want to hide it.
	Active = 1,
	/// An item that needs attention conveys very important information such as low battery.
	NeedsAttention = 2,
};
Q_ENUM_NS(Enum);
} // namespace Status

///! Category of a StatusNotifierItem.
/// See @@StatusNotifierItem.category.
namespace Category { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	/// Hardware controls like battery indicators or volume control.
	Hardware = 0,
	/// System services such as IMEs or disk indexing.
	SystemServices = 1,
	/// The fallback category for general applications or anything that does
	/// not fit into a different category.
	ApplicationStatus = 2,
	/// Communication related applications such as instant messengers or email clients.
	Communications = 3,
};
Q_ENUM_NS(Enum);
} // namespace Category

///! An item in the system tray.
/// On Windows, an icon in the notification area, see @@Quickshell.Windows.NotifyIcon.
///
/// Programs draw their own menus, so @@hasMenu is false and @@menu is null, but @@display()
/// still opens the program's menu. Every item is in the `ApplicationStatus` category, and
/// hidden icons are `Passive`.
class StatusNotifierItem: public QObject {
	Q_OBJECT;
	// clang-format off
	/// A name unique to the application, such as its name.
	///
	/// On Windows, the icon's GUID if it has one, otherwise the program's file name without
	/// `.exe` and the icon's number, such as `Everything_0`.
	Q_PROPERTY(QString id READ id CONSTANT);
	/// Text that describes the application.
	Q_PROPERTY(QString title READ title CONSTANT);
	Q_PROPERTY(qs::windows::systray::Status::Enum status READ default NOTIFY statusChanged BINDABLE bindableStatus);
	Q_PROPERTY(qs::windows::systray::Category::Enum category READ category CONSTANT);
	/// Icon source string, usable as an Image source.
	Q_PROPERTY(QString icon READ default NOTIFY iconChanged BINDABLE bindableIcon);
	/// The first line of the tooltip.
	Q_PROPERTY(QString tooltipTitle READ default NOTIFY tooltipTitleChanged BINDABLE bindableTooltipTitle);
	/// The rest of the tooltip.
	Q_PROPERTY(QString tooltipDescription READ default NOTIFY tooltipDescriptionChanged BINDABLE bindableTooltipDescription);
	/// If this tray item has an associated menu accessible via @@display() or @@menu.
	///
	/// Always false on Windows, where @@display() opens the program's own menu.
	Q_PROPERTY(bool hasMenu READ hasMenu CONSTANT);
	/// A handle to the menu associated with this tray item, if any. Always null on Windows.
	Q_PROPERTY(qs::menu::QsMenuHandle* menu READ menu CONSTANT);
	/// If this tray item only offers a menu and activation will do nothing.
	Q_PROPERTY(bool onlyMenu READ onlyMenu CONSTANT);
	// clang-format on
	QML_NAMED_ELEMENT(SystemTrayItem);
	QML_UNCREATABLE("SystemTrayItems can only be acquired from SystemTray");

public:
	explicit StatusNotifierItem(NotifyIcon* icon, QObject* parent = nullptr);

	/// Primary activation action, generally triggered via a left click.
	Q_INVOKABLE void activate();
	/// Secondary activation action, generally triggered via a middle click.
	Q_INVOKABLE void secondaryActivate();
	/// Scroll action, such as changing volume on a mixer. Does nothing on Windows.
	Q_INVOKABLE void scroll(qint32 delta, bool horizontal) const;
	/// Display a platform menu at the given location relative to the parent window.
	///
	/// On Windows, asks the program to open its own menu there. Many programs open it at the
	/// mouse cursor instead.
	Q_INVOKABLE void display(QObject* parentWindow, qint32 relativeX, qint32 relativeY);

	[[nodiscard]] QString id() const { return this->mId; }
	[[nodiscard]] QString title() const { return this->icon->appName(); }
	[[nodiscard]] static Category::Enum category() { return Category::ApplicationStatus; }
	[[nodiscard]] static bool hasMenu() { return false; }
	[[nodiscard]] static menu::QsMenuHandle* menu() { return nullptr; }
	[[nodiscard]] static bool onlyMenu() { return false; }

	[[nodiscard]] QBindable<Status::Enum> bindableStatus() const { return &this->bStatus; }
	[[nodiscard]] QBindable<QString> bindableIcon() const { return &this->bIcon; }
	[[nodiscard]] QBindable<QString> bindableTooltipTitle() const { return &this->bTooltipTitle; }
	[[nodiscard]] QBindable<QString> bindableTooltipDescription() const {
		return &this->bTooltipDescription;
	}

signals:
	void ready();

	void statusChanged();
	void iconChanged();
	void tooltipTitleChanged();
	void tooltipDescriptionChanged();

private:
	NotifyIcon* icon;
	QString mId;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, Status::Enum, bStatus, &StatusNotifierItem::statusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, QString, bIcon, &StatusNotifierItem::iconChanged);
	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, QString, bTooltipTitle, &StatusNotifierItem::tooltipTitleChanged);
	Q_OBJECT_BINDABLE_PROPERTY(StatusNotifierItem, QString, bTooltipDescription, &StatusNotifierItem::tooltipDescriptionChanged);
	// clang-format on
};

} // namespace qs::windows::systray
