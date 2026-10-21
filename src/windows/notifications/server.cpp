#include "server.hpp"
#include <algorithm>
#include <functional>

#include <qcontainerfwd.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qqmlengine.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"
#include "../../core/model.hpp"
#include "../toasts.hpp"
#include "notification.hpp"

namespace qs::windows::notifications {

namespace {
QS_LOGGING_CATEGORY(logNotifications, "quickshell.service.notifications", QtWarningMsg);
}

NotificationServer::NotificationServer() {
	auto* toasts = ToastService::instance();

	QObject::connect(toasts, &ToastService::toastAdded, this, &NotificationServer::onToastAdded);

	QObject::connect(
	    toasts,
	    &ToastService::toastReplaced,
	    this,
	    &NotificationServer::onToastReplaced
	);

	QObject::connect(toasts, &ToastService::toastRemoved, this, &NotificationServer::onToastRemoved);

	qCInfo(logNotifications) << "Starting notification server";
}

NotificationServer* NotificationServer::instance() {
	static auto* instance = new NotificationServer(); // NOLINT
	return instance;
}

void NotificationServer::switchGeneration(bool reEmit, const std::function<void()>& clearHook) {
	auto notifications = this->mNotifications.valueList();
	this->mNotifications.valueList().clear();
	this->idMap.clear();

	clearHook();

	if (reEmit) {
		for (auto* notification: notifications) {
			notification->setLastGeneration();
			notification->setTracked(false);
			emit this->notification(notification);

			if (!notification->isTracked()) {
				delete notification;
			} else {
				this->idMap.insert(notification->toastId(), notification);
				this->mNotifications.insertObject(notification);
			}
		}
	} else {
		for (auto* notification: notifications) {
			delete notification;
		}
	}
}

ObjectModel<Notification>* NotificationServer::trackedNotifications() {
	return &this->mNotifications;
}

void NotificationServer::deleteNotification(
    Notification* notification,
    NotificationCloseReason::Enum reason,
    bool removeToast
) {
	if (this->idMap.value(notification->toastId()) != notification) return;

	emit notification->closed(reason);

	this->mNotifications.removeObject(notification);
	this->idMap.remove(notification->toastId());

	if (removeToast) ToastService::instance()->remove(notification->toastId());
	notification->retainedDestroy();
}

void NotificationServer::invoke(
    Notification* notification,
    qsizetype button,
    const QVariantMap& inputs
) {
	auto* toast = NotificationServer::findToast(notification->toastId());

	if (!toast) {
		qCWarning(logNotifications) << "No toast to invoke for" << notification;
	} else if (button == -1) {
		toast->activate();
	} else if (button < toast->actions().length()) {
		toast->actions().at(button)->invoke(inputs);
	}

	// The toast already removed itself.
	if (!notification->bindableResident().value()) {
		this->deleteNotification(notification, NotificationCloseReason::Dismissed, false);
	}
}

Toast* NotificationServer::findToast(quint32 id) {
	const auto& toasts = ToastService::instance()->model()->valueList();
	auto toast = std::ranges::find(toasts, id, &Toast::id);
	return toast == toasts.end() ? nullptr : *toast;
}

void NotificationServer::onToastAdded(const ToastData& data) {
	auto* notification = new Notification(data.id, this);
	QQmlEngine::setObjectOwnership(notification, QQmlEngine::CppOwnership);

	notification->updateProperties(
	    data,
	    NotificationServer::findToast(data.id),
	    this->support.bodyMarkup,
	    this->support.inlineReply
	);

	emit this->notification(notification);

	if (!notification->isTracked()) {
		delete notification;
	} else {
		this->idMap.insert(data.id, notification);
		this->mNotifications.insertObject(notification);
	}
}

void NotificationServer::onToastReplaced(quint32 oldId, const ToastData& data) {
	auto* notification = this->idMap.take(oldId);
	if (!notification) {
		this->onToastAdded(data);
		return;
	}

	notification->setToastId(data.id);
	this->idMap.insert(data.id, notification);

	notification->updateProperties(
	    data,
	    NotificationServer::findToast(data.id),
	    this->support.bodyMarkup,
	    this->support.inlineReply
	);
}

void NotificationServer::onToastRemoved(quint32 id, bool expired) {
	if (auto* notification = this->idMap.value(id)) {
		auto reason =
		    expired ? NotificationCloseReason::Expired : NotificationCloseReason::CloseRequested;
		this->deleteNotification(notification, reason, false);
	}
}

} // namespace qs::windows::notifications
