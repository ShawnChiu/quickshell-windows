#include "item.hpp"

#include <qfileinfo.h>
#include <qlogging.h>
#include <qobject.h>
#include <qpoint.h>
#include <qstring.h>
#include <qtypes.h>
#include <qwindow.h>

#include "../../window/proxywindow.hpp"
#include "../notify_icons.hpp"

namespace qs::windows::systray {

StatusNotifierItem::StatusNotifierItem(NotifyIcon* icon, QObject* parent)
    : QObject(parent)
    , icon(icon) {
	this->mId = icon->guid();
	if (this->mId.isEmpty()) {
		this->mId =
		    QFileInfo(icon->appPath()).completeBaseName() + '_' + QString::number(icon->iconId());
	}

	this->bStatus.setBinding([icon]() {
		return icon->bindableHidden().value() ? Status::Passive : Status::Active;
	});

	this->bIcon.setBinding([icon]() { return icon->bindableIcon().value(); });

	this->bTooltipTitle.setBinding([icon]() {
		return icon->bindableTooltip().value().section('\n', 0, 0).trimmed();
	});

	this->bTooltipDescription.setBinding([icon]() {
		auto description = icon->bindableTooltip().value().section('\n', 1);
		return description.replace("\r\n", "\n").trimmed();
	});
}

void StatusNotifierItem::activate() { this->icon->click(); }

void StatusNotifierItem::secondaryActivate() { this->icon->middleClick(); }

void StatusNotifierItem::scroll(qint32 /*delta*/, bool /*horizontal*/) const {}

void StatusNotifierItem::display(QObject* parentWindow, qint32 relativeX, qint32 relativeY) {
	QWindow* window = nullptr;

	if (parentWindow == nullptr) {
		qCritical() << "Cannot display a tray menu with null parent window.";
		return;
	} else if (auto* proxy = ProxyWindowBase::forObject(parentWindow)) {
		window = proxy->backingWindow();
	} else {
		qCritical() << "SystemTrayItem.display() must be called with a window.";
		return;
	}

	if (window == nullptr) {
		qCritical() << "Cannot display a tray menu from a parent window that is not visible.";
		return;
	}

	auto point = window->mapToGlobal(QPoint(relativeX, relativeY));
	this->icon->contextMenu(point.x(), point.y());
}

} // namespace qs::windows::systray
