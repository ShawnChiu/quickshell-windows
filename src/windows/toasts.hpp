#pragma once

#include <qcontainerfwd.h>
#include <qdatetime.h>
#include <qimage.h>
#include <qlist.h>
#include <qobject.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

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
	void finishSnapshot();

	ToastServicePrivate* impl;
	QList<ToastData> mToasts;
	QTimer pollTimer;
	AccessStatus mAccessStatus = Unspecified;
	bool mReady = false;
	bool listed = false;
	bool snapshotRunning = false;
	const char* pendingSource = nullptr;

	friend class ToastServicePrivate;
};

} // namespace qs::windows
