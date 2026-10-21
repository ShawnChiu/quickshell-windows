#include "notification.hpp"

#include <qcontainerfwd.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../toasts.hpp"
#include "server.hpp"

namespace qs::windows::notifications {

namespace {

// Tray balloons Windows turned into toasts, which have nothing to open.
bool canOpen(const ToastData& data) {
	return !data.launch.isEmpty() || !data.aumid.startsWith(u"NotifyIconGeneratedAumid_");
}

bool isCritical(const QString& scenario) {
	return scenario.compare(u"alarm", Qt::CaseInsensitive) == 0
	    || scenario.compare(u"incomingCall", Qt::CaseInsensitive) == 0
	    || scenario.compare(u"urgent", Qt::CaseInsensitive) == 0;
}

} // namespace

QString NotificationUrgency::toString(NotificationUrgency::Enum value) {
	switch (value) {
	case NotificationUrgency::Low: return "Low";
	case NotificationUrgency::Normal: return "Normal";
	case NotificationUrgency::Critical: return "Critical";
	default: return "Invalid notification urgency";
	}
}

QString NotificationCloseReason::toString(NotificationCloseReason::Enum value) {
	switch (value) {
	case NotificationCloseReason::Expired: return "Expired";
	case NotificationCloseReason::Dismissed: return "Dismissed";
	case NotificationCloseReason::CloseRequested: return "CloseRequested";
	default: return "Invalid notification close reason";
	}
}

void NotificationIcon::setImage(const QImage& image) {
	if (image == this->image) return;
	this->image = image;
	this->imageChanged();
}

QImage
NotificationIcon::requestImage(const QString& /*unused*/, QSize* size, const QSize& /*unused*/) {
	if (size != nullptr) *size = this->image.size();
	return this->image;
}

QString NotificationAction::identifier() const { return this->mIdentifier; }
QString NotificationAction::text() const { return this->mText; }

void NotificationAction::invoke() {
	if (this->notification->isRetained()) {
		qCritical() << "Cannot invoke destroyed notification" << this;
		return;
	}

	NotificationServer::instance()->invoke(this->notification, this->mButton);
}

void NotificationAction::setText(const QString& text) {
	if (text == this->mText) return;

	this->mText = text;
	emit this->textChanged();
}

void Notification::expire() { this->close(NotificationCloseReason::Expired); }
void Notification::dismiss() { this->close(NotificationCloseReason::Dismissed); }

void Notification::close(NotificationCloseReason::Enum reason) {
	if (this->isRetained()) {
		qCritical() << "Cannot close destroyed notification" << this;
		return;
	}

	this->mCloseReason = reason;

	if (reason != 0) {
		NotificationServer::instance()
		    ->deleteNotification(this, reason, reason == NotificationCloseReason::Dismissed);
	}
}

void Notification::sendInlineReply(const QString& replyText) {
	if (!NotificationServer::instance()->support.inlineReply) {
		qCritical() << "Inline reply support disabled on server";
		return;
	}

	if (!this->bHasInlineReply) {
		qCritical() << "Cannot send reply to notification without inline-reply action";
		return;
	}

	if (this->isRetained()) {
		qCritical() << "Cannot send reply to destroyed notification" << this;
		return;
	}

	NotificationServer::instance()
	    ->invoke(this, this->mReplyButton, {{this->mReplyInput, replyText}});
}

void Notification::updateProperties(
    const ToastData& data,
    const Toast* toast,
    bool bodyMarkup,
    bool inlineReply
) {
	Qt::beginPropertyUpdateGroup();

	auto body = data.texts.mid(1).join('\n');
	if (bodyMarkup) body = body.toHtmlEscaped();

	this->bExpireTimeout = !data.scenario.isEmpty() ? 0 : data.duration == u"long" ? 25000 : -1;
	this->bAppName = data.appName;
	this->bSummary = data.texts.value(0);
	this->bBody = body;
	this->bDesktopEntry = data.aumid;
	this->bUrgency =
	    isCritical(data.scenario) ? NotificationUrgency::Critical : NotificationUrgency::Normal;

	this->mIcon.setImage(data.logo);
	this->bAppIcon = this->mIcon.isNull() ? QString() : this->mIcon.url();
	this->bImage = toast ? toast->image() : QString();

	auto buttons = toast ? toast->actions() : QList<ToastAction*>();

	this->mReplyButton = -1;
	this->mReplyInput.clear();
	this->bInlineReplyPlaceholder = QString();

	auto textInputs = QList<ToastInput*>();
	if (toast) {
		for (auto* input: toast->inputs()) {
			if (input->type() == u"text") textInputs.append(input);
		}
	}

	if (inlineReply && textInputs.length() == 1) {
		auto* input = textInputs.first();
		for (auto i = 0; i != buttons.length(); i++) {
			if (buttons.at(i)->inputId() != input->id()) continue;

			this->mReplyButton = i;
			this->mReplyInput = input->id();
			this->bInlineReplyPlaceholder = input->placeholder();
			break;
		}
	}

	this->bHasInlineReply = this->mReplyButton != -1;

	auto actions = QList<std::pair<QString, qsizetype>>();
	if (canOpen(data)) actions.append({QStringLiteral("default"), -1});

	for (auto i = 0; i != buttons.length(); i++) {
		if (i != this->mReplyButton) actions.append({buttons.at(i)->arguments(), i});
	}

	bool actionsChanged = false;
	auto deletedActions = QVector<NotificationAction*>();

	qsizetype ai = 0;
	for (const auto& [identifier, button]: actions) {
		auto text = button == -1 ? QStringLiteral("Open") : buttons.at(button)->text();
		auto* action = ai < this->mActions.length() ? this->mActions.at(ai) : nullptr;

		if (action && identifier == action->identifier()) {
			action->setText(text);
			action->setButton(button);
		} else {
			auto* newAction = new NotificationAction(identifier, button, this);
			newAction->setText(text);

			if (action) {
				deletedActions.push_back(action);
				this->mActions.replace(ai, newAction);
			} else {
				this->mActions.push_back(newAction);
			}

			actionsChanged = true;
		}

		ai++;
	}

	for (auto i = this->mActions.length(); i > ai; i--) {
		deletedActions.push_back(this->mActions.at(i - 1));
		this->mActions.remove(i - 1);
		actionsChanged = true;
	}

	Qt::endPropertyUpdateGroup();

	if (actionsChanged) emit this->actionsChanged();

	for (auto* action: deletedActions) {
		delete action;
	}
}

quint32 Notification::id() const { return this->mId; }
bool Notification::isTracked() const { return this->mCloseReason == 0; }
QList<NotificationAction*> Notification::actions() const { return this->mActions; }
NotificationCloseReason::Enum Notification::closeReason() const { return this->mCloseReason; }

void Notification::setTracked(bool tracked) {
	this->close(
	    tracked ? static_cast<NotificationCloseReason::Enum>(0) : NotificationCloseReason::Dismissed
	);
}

bool Notification::isLastGeneration() const { return this->mLastGeneration; }
void Notification::setLastGeneration() { this->mLastGeneration = true; }

} // namespace qs::windows::notifications
