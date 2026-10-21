#pragma once

#include <qimage.h>
#include <qmutex.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <quuid.h>

#include "../core/doc.hpp"
#include "../core/imageprovider.hpp"
#include "../core/model.hpp"

namespace qs::windows {

// An icon as its owner last registered it through Shell_NotifyIcon.
struct NotifyIconData {
	quint64 key = 0;
	quintptr hwnd = 0;
	quint32 id = 0;
	QUuid guid;
	quint32 processId = 0;
	quint32 callbackMessage = 0;
	quint32 version = 0;
	QString tooltip;
	QImage icon;
	bool hidden = false;
};

// Serves an icon's image, which may be read from the image loader's thread.
class NotifyIconImage: public QsIndexedImageHandle {
public:
	explicit NotifyIconImage(): QsIndexedImageHandle(QQuickImageProvider::Image) {}

	void setImage(QImage image);
	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	QMutex mutex;
	QImage image;
};

///! An icon in the system tray.
/// An icon a program shows in the notification area through [Shell_NotifyIcon].
///
/// The methods send the program the mouse events Explorer's tray would. What they do is up to
/// the program, and menus are the program's own: @@contextMenu() asks it to open its menu.
/// Like @@Hwnd.activate(), they only let the program take the foreground when called from a
/// click handler.
///
/// [Shell_NotifyIcon]: https://learn.microsoft.com/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw
class NotifyIcon: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("NotifyIcons can only be acquired from NotifyIcons");
	// clang-format off
	/// The id of the process that shows the icon.
	Q_PROPERTY(qint32 processId READ processId CONSTANT);
	/// The program's file description, as Task Manager shows it, or its file name without `.exe`
	/// if it has none.
	Q_PROPERTY(QString appName READ appName CONSTANT);
	/// The full path of the program, or `""` if it could not be read.
	Q_PROPERTY(QString appPath READ appPath CONSTANT);
	/// The GUID the program identifies the icon by, or `""` if it has none. Most programs have none.
	Q_PROPERTY(QString guid READ guid CONSTANT);
	Q_PROPERTY(QString tooltip READ default NOTIFY tooltipChanged BINDABLE bindableTooltip);
	/// An image url for the icon, or `""` if the program set none.
	Q_PROPERTY(QString icon READ default NOTIFY iconChanged BINDABLE bindableIcon);
	/// If the program hid the icon. Explorer does not show hidden icons.
	Q_PROPERTY(bool hidden READ default NOTIFY hiddenChanged BINDABLE bindableHidden);
	// clang-format on

public:
	explicit NotifyIcon(const NotifyIconData& data, QObject* parent);

	/// Sends a left click.
	Q_INVOKABLE void click();
	/// Sends a double click. Call @@click() for the first click, as a `MouseArea` sends `clicked`
	/// before `doubleClicked`.
	Q_INVOKABLE void doubleClick();
	/// Sends a middle click.
	Q_INVOKABLE void middleClick();
	/// Sends a right click, which opens the program's menu if it has one.
	///
	/// `x` and `y` are where the menu should open, in global coordinates as `Item.mapToGlobal()`
	/// gives them. Many programs open it at the mouse cursor instead.
	Q_INVOKABLE void contextMenu(qreal x, qreal y);

	[[nodiscard]] quint64 key() const { return this->mKey; }
	[[nodiscard]] quint32 iconId() const { return this->mId; }

	[[nodiscard]] qint32 processId() const { return this->mProcessId; }
	[[nodiscard]] QString appName() const { return this->mAppName; }
	[[nodiscard]] QString appPath() const { return this->mAppPath; }
	[[nodiscard]] QString guid() const { return this->mGuid; }

	[[nodiscard]] QBindable<QString> bindableTooltip() const { return &this->bTooltip; }
	[[nodiscard]] QBindable<QString> bindableIcon() const { return &this->bIcon; }
	[[nodiscard]] QBindable<bool> bindableHidden() const { return &this->bHidden; }

	void update(const NotifyIconData& data);

signals:
	void tooltipChanged();
	void iconChanged();
	void hiddenChanged();

private:
	void send(quint32 message, qint32 x, qint32 y) const;

	quint64 mKey;
	quintptr mHwnd;
	quint32 mId;
	quint32 mCallbackMessage = 0;
	quint32 mVersion = 0;
	qint32 mProcessId;
	QString mAppName;
	QString mAppPath;
	QString mGuid;
	QImage mImage;
	NotifyIconImage image;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(NotifyIcon, QString, bTooltip, &NotifyIcon::tooltipChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NotifyIcon, QString, bIcon, &NotifyIcon::iconChanged);
	Q_OBJECT_BINDABLE_PROPERTY(NotifyIcon, bool, bHidden, &NotifyIcon::hiddenChanged);
	// clang-format on
};

class NotifyIconServicePrivate;

// Process wide icon list behind NotifyIcons, so the tray is only intercepted once across reloads.
class NotifyIconService: public QObject {
	Q_OBJECT;

public:
	static NotifyIconService* instance();

	// In the order they were added.
	[[nodiscard]] ObjectModel<NotifyIcon>* icons() { return &this->mIcons; }

private:
	explicit NotifyIconService();

	void updateIcon(const NotifyIconData& icon);
	void removeIcon(quint64 key);

	NotifyIconServicePrivate* impl;
	ObjectModel<NotifyIcon> mIcons {this};

	friend class NotifyIconServicePrivate;
};

///! Icons in the system tray.
/// The icons programs show in the notification area. Explorer's tray keeps working unchanged
/// beside this.
///
/// - Only one program at a time can list them. Do not run this alongside other programs that
///   show the tray, such as yasb's tray widget.
/// - Programs already running are asked to show their icons again, as when Explorer restarts.
///   A program that ignores this appears once it is restarted.
/// - Only some of Windows' own icons are listed, such as Bluetooth and volume.
///
/// Programs draw their own menus, so there is no menu to show: @@NotifyIcon.contextMenu() opens
/// the program's.
class NotifyIconsQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(NotifyIcons);
	QML_SINGLETON;
	// clang-format off
	/// The icons, in the order they were added.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::NotifyIcon>*);
	Q_PROPERTY(UntypedObjectModel* icons READ icons CONSTANT);
	// clang-format on

public:
	explicit NotifyIconsQml(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] static ObjectModel<NotifyIcon>* icons() {
		return NotifyIconService::instance()->icons();
	}
};

} // namespace qs::windows
