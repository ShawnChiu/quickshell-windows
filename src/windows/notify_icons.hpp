#pragma once

#include <qimage.h>
#include <qlist.h>
#include <qobject.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <quuid.h>

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

class NotifyIconServicePrivate;

// Process wide icon list, so the tray is only intercepted once across reloads.
class NotifyIconService: public QObject {
	Q_OBJECT;

public:
	static NotifyIconService* instance();

	// In the order they were added.
	[[nodiscard]] const QList<NotifyIconData>& icons() const { return this->mIcons; }

signals:
	void iconAdded(const qs::windows::NotifyIconData& icon);
	void iconChanged(const qs::windows::NotifyIconData& icon);
	void iconRemoved(quint64 key);

private:
	explicit NotifyIconService();

	void updateIcon(const NotifyIconData& icon);
	void removeIcon(quint64 key);

	NotifyIconServicePrivate* impl;
	QList<NotifyIconData> mIcons;

	friend class NotifyIconServicePrivate;
};

} // namespace qs::windows
