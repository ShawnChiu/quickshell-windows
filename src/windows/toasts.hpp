#pragma once

#include <utility>

#include <qcontainerfwd.h>
#include <qdatetime.h>
#include <qimage.h>
#include <qlist.h>
#include <qmap.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../core/doc.hpp"
#include "../core/imageprovider.hpp"
#include "../core/model.hpp"

namespace qs::windows {

struct ToastActionData {
	QString content;
	QString arguments;
	QString activationType;
	QString inputId;
};

struct ToastInputData {
	QString id;
	QString type;
	QString placeholder;
	QString defaultInput;
};

// A toast in the notification center. Everything after texts comes from the toast's XML, and
// stays empty if the notification database could not be read.
struct ToastData {
	quint32 id = 0;
	QString aumid;
	QString appName;
	QImage logo;
	QDateTime arrival;
	QDateTime expiry;
	QList<QString> texts;
	QString image;
	QString scenario;
	QString duration;
	QString launch;
	QString launchType;
	QList<ToastActionData> actions;
	QList<ToastInputData> inputs;
	QString tag;
	QString group;
};

// Serves a toast's app logo, which never changes once the toast is listed.
class ToastLogo: public QsImageHandle {
public:
	explicit ToastLogo(QImage image)
	    : QsImageHandle(QQuickImageProvider::Image)
	    , image(std::move(image)) {}

	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	QImage image;
};

class Toast;

///! A button of a Toast.
class ToastAction: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("ToastActions can only be acquired from Toasts");
	// clang-format off
	Q_PROPERTY(QString text READ text CONSTANT);
	/// What the app receives when the button is pressed. For `protocol` buttons, the uri to open.
	Q_PROPERTY(QString arguments READ arguments CONSTANT);
	/// `foreground` or `background` for buttons the app handles, `protocol` for buttons that open
	/// a uri.
	Q_PROPERTY(QString activationType READ activationType CONSTANT);
	/// The id of the @@ToastInput this button sends, such as a reply box's send button, or `""`.
	Q_PROPERTY(QString inputId READ inputId CONSTANT);
	// clang-format on

public:
	explicit ToastAction(ToastActionData data, Toast* toast);

	/// Presses the button and removes the toast from the notification center.
	///
	/// `inputs` maps the ids of the toast's @@Toast.inputs to what was entered or selected.
	/// Inputs left out send their @@ToastInput.defaultInput.
	///
	/// Buttons an app handles only reach Win32 apps that register for them, such as Firefox.
	/// For other apps, including Store apps, this opens the app instead.
	Q_INVOKABLE void invoke(const QVariantMap& inputs = {});

	[[nodiscard]] QString text() const { return this->data.content; }
	[[nodiscard]] QString arguments() const { return this->data.arguments; }
	[[nodiscard]] QString activationType() const { return this->data.activationType; }
	[[nodiscard]] QString inputId() const { return this->data.inputId; }

private:
	ToastActionData data;
	Toast* toast;
};

///! A text box or choice of a Toast.
class ToastInput: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("ToastInputs can only be acquired from Toasts");
	// clang-format off
	/// The id to pass to @@ToastAction.invoke().
	Q_PROPERTY(QString id READ id CONSTANT);
	/// `text` for a text box, `selection` for a choice. The choices are not listed.
	Q_PROPERTY(QString type READ type CONSTANT);
	/// Hint text for an empty text box.
	Q_PROPERTY(QString placeholder READ placeholder CONSTANT);
	/// The text, or the id of the choice, the input starts with.
	Q_PROPERTY(QString defaultInput READ defaultInput CONSTANT);
	// clang-format on

public:
	explicit ToastInput(ToastInputData data, QObject* parent)
	    : QObject(parent)
	    , data(std::move(data)) {}

	[[nodiscard]] QString id() const { return this->data.id; }
	[[nodiscard]] QString type() const { return this->data.type; }
	[[nodiscard]] QString placeholder() const { return this->data.placeholder; }
	[[nodiscard]] QString defaultInput() const { return this->data.defaultInput; }

private:
	ToastInputData data;
};

///! A notification in the Windows notification center.
/// A toast notification an app sent, as listed in the notification center.
///
/// Toasts never change. When an app updates a toast, it is removed and a new one is added.
class Toast: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("Toasts can only be acquired from Toasts");
	// clang-format off
	/// The id Windows gives the toast.
	Q_PROPERTY(quint32 id READ id CONSTANT);
	/// The app's [AppUserModelID], which is also its @@Quickshell.DesktopEntry.id.
	///
	/// [AppUserModelID]: https://learn.microsoft.com/windows/win32/shell/appids
	Q_PROPERTY(QString appId READ appId CONSTANT);
	/// The app's display name.
	Q_PROPERTY(QString appName READ appName CONSTANT);
	/// An image url for the toast's logo, usually the app's, or `""` if there is none.
	Q_PROPERTY(QString appIcon READ appIcon CONSTANT);
	Q_PROPERTY(QDateTime arrivalTime READ arrivalTime CONSTANT);
	/// When Windows removes the toast from the notification center.
	Q_PROPERTY(QDateTime expiryTime READ expiryTime CONSTANT);
	/// `alarm`, `reminder`, `incomingCall` or `urgent` for toasts that stay on screen until
	/// acted on, otherwise `""`.
	Q_PROPERTY(QString scenario READ scenario CONSTANT);
	/// The toast's lines of text. The first is its title.
	Q_PROPERTY(QList<QString> texts READ texts CONSTANT);
	/// A file url for the toast's image, or `""` if it has none or it is not a local file.
	Q_PROPERTY(QString image READ image CONSTANT);
	/// The toast's buttons, without the snooze and dismiss buttons Windows provides.
	Q_PROPERTY(QList<qs::windows::ToastAction*> actions READ actions CONSTANT);
	Q_PROPERTY(QList<qs::windows::ToastInput*> inputs READ inputs CONSTANT);
	// clang-format on

public:
	explicit Toast(ToastData data, QObject* parent);

	/// Does what clicking the toast does and removes it from the notification center. As
	/// with @@ToastAction.invoke(), this may open the app instead.
	Q_INVOKABLE void activate();
	/// Removes the toast from the notification center.
	Q_INVOKABLE void remove();

	[[nodiscard]] const ToastData& data() const { return this->mData; }

	[[nodiscard]] quint32 id() const { return this->mData.id; }
	[[nodiscard]] QString appId() const { return this->mData.aumid; }
	[[nodiscard]] QString appName() const { return this->mData.appName; }
	[[nodiscard]] QString appIcon() const;
	[[nodiscard]] QDateTime arrivalTime() const { return this->mData.arrival; }
	[[nodiscard]] QDateTime expiryTime() const { return this->mData.expiry; }
	[[nodiscard]] QString scenario() const { return this->mData.scenario; }
	[[nodiscard]] QList<QString> texts() const { return this->mData.texts; }
	[[nodiscard]] QString image() const;
	[[nodiscard]] QList<ToastAction*> actions() const { return this->mActions; }
	[[nodiscard]] QList<ToastInput*> inputs() const { return this->mInputs; }

	void invoke(const QString& activationType, const QString& arguments, const QVariantMap& inputs);

private:
	ToastData mData;
	ToastLogo logo;
	QList<ToastAction*> mActions;
	QList<ToastInput*> mInputs;
};

class ToastServicePrivate;

// Process wide list of the notification center's toasts, so it is only watched once across reloads.
class ToastService: public QObject {
	Q_OBJECT;

public:
	// Same values as UserNotificationListenerAccessStatus.
	enum AccessStatus : quint8 {
		Unspecified = 0,
		Allowed = 1,
		Denied = 2,
	};

	static ToastService* instance();

	// In arrival order. Toasts already there when they can first be read are listed without
	// toastAdded.
	[[nodiscard]] const QList<ToastData>& toasts() const { return this->mToasts; }
	[[nodiscard]] ObjectModel<Toast>* model() { return &this->mModel; }
	[[nodiscard]] bool isReady() const { return this->mReady; }
	[[nodiscard]] AccessStatus accessStatus() const { return this->mAccessStatus; }

	void remove(quint32 id);
	void clear();

signals:
	void ready();
	void accessStatusChanged();
	void toastAdded(const qs::windows::ToastData& data);
	void toastReplaced(quint32 oldId, const qs::windows::ToastData& data);
	void toastRemoved(quint32 id, bool expired);

private:
	explicit ToastService();

	void setAccessStatus(AccessStatus status);
	void requestSnapshot(const char* source);
	void
	applySnapshot(const QList<quint32>& ids, QList<ToastData> added, bool allowed, qint64 elapsed);
	void updateModel(const QList<ToastData>& removed, const QList<ToastData>& added);
	void finishSnapshot();

	ToastServicePrivate* impl;
	QList<ToastData> mToasts;
	ObjectModel<Toast> mModel {this};
	QTimer pollTimer;
	AccessStatus mAccessStatus = Unspecified;
	bool mReady = false;
	bool listed = false;
	bool snapshotRunning = false;
	const char* pendingSource = nullptr;

	friend class ToastServicePrivate;
};

///! Notifications in the Windows notification center.
/// The toast notifications apps sent, as listed in the notification center.
///
/// Windows still shows its own banners for them. To only see a config's popups, turn off
/// "Show notification banners" for each app in Settings, System, Notifications, or turn on
/// Do not disturb, which keeps toasts listed here.
///
/// Reading them needs "Let desktop apps access your notifications" in Settings, Privacy &
/// security, Notifications. Windows asks the first time this is used.
///
/// Buttons are read from Windows' notification database, which is undocumented, so a toast
/// may have no @@Toast.actions. Store apps can only be opened, not sent a button press.
/// Like @@Hwnd.activate(), @@Toast.activate() and @@ToastAction.invoke() only let the app
/// take the foreground when called from a click handler.
class ToastsQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Toasts);
	QML_SINGLETON;
	// clang-format off
	/// The toasts, oldest first.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::Toast>*);
	Q_PROPERTY(UntypedObjectModel* toasts READ toasts CONSTANT);
	/// If apps may read notifications. While false, @@toasts is empty.
	Q_PROPERTY(bool allowed READ allowed NOTIFY allowedChanged);
	// clang-format on

public:
	explicit ToastsQml(QObject* parent = nullptr);

	/// Removes every toast from the notification center, including those of other apps.
	Q_INVOKABLE static void clear() { ToastService::instance()->clear(); }

	[[nodiscard]] static ObjectModel<Toast>* toasts() { return ToastService::instance()->model(); }
	[[nodiscard]] static bool allowed() {
		return ToastService::instance()->accessStatus() == ToastService::Allowed;
	}

signals:
	void allowedChanged();
};

} // namespace qs::windows
