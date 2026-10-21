#pragma once

#include <functional>

#include <qcontainerfwd.h>
#include <qhash.h>
#include <qobject.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/model.hpp"
#include "../toasts.hpp"
#include "notification.hpp"

namespace qs::windows::notifications {

struct NotificationServerSupport {
	bool persistence = false;
	bool body = true;
	bool bodyMarkup = false;
	bool bodyHyperlinks = false;
	bool bodyImages = false;
	bool actions = false;
	bool actionIcons = false;
	bool image = false;
	bool inlineReply = false;
	QVector<QString> extraHints;
};

// Follows ToastService, keyed by each notification's current toast id.
class NotificationServer: public QObject {
	Q_OBJECT;

public:
	static NotificationServer* instance();

	void switchGeneration(bool reEmit, const std::function<void()>& clearHook);
	ObjectModel<Notification>* trackedNotifications();
	void deleteNotification(
	    Notification* notification,
	    NotificationCloseReason::Enum reason,
	    bool removeToast
	);

	// Presses a button of the notification's toast, or the toast itself for -1.
	void invoke(Notification* notification, qsizetype button, const QVariantMap& inputs = {});

	NotificationServerSupport support;

signals:
	void notification(Notification* notification);

private slots:
	void onToastAdded(const qs::windows::ToastData& data);
	void onToastReplaced(quint32 oldId, const qs::windows::ToastData& data);
	void onToastRemoved(quint32 id, bool expired);

private:
	explicit NotificationServer();

	static Toast* findToast(quint32 id);

	QHash<quint32, Notification*> idMap;
	ObjectModel<Notification> mNotifications {this};
};

} // namespace qs::windows::notifications
