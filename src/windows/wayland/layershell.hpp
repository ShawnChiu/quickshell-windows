#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../window/panelinterface.hpp"

// Quickshell.Wayland on Windows only exists so configs that import it still load, the same
// way the module is present but inert under X11. Nothing here affects any window. Windows
// behavior lives in Quickshell.Windows and the platform independent PanelWindow properties.
namespace qs::windows::wayland {

namespace WlrLayer { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Background = 0,
	Bottom = 1,
	Top = 2,
	Overlay = 3,
};
Q_ENUM_NS(Enum);

} // namespace WlrLayer

namespace WlrKeyboardFocus { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	None = 0,
	Exclusive = 1,
	OnDemand = 2,
};
Q_ENUM_NS(Enum);

} // namespace WlrKeyboardFocus

class WlrLayershell: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::windows::wayland::WlrLayer::Enum layer MEMBER mLayer NOTIFY layerChanged);
	Q_PROPERTY(QString namespace MEMBER mNamespace NOTIFY namespaceChanged);
	Q_PROPERTY(qs::windows::wayland::WlrKeyboardFocus::Enum keyboardFocus MEMBER mKeyboardFocus NOTIFY keyboardFocusChanged);
	Q_PROPERTY(ExclusionMode::Enum exclusionMode MEMBER mExclusionMode NOTIFY exclusionModeChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("WlrLayershell is not available on Windows.");
	QML_ATTACHED(WlrLayershell);

public:
	explicit WlrLayershell(QObject* parent): QObject(parent) {}

	static WlrLayershell* qmlAttachedProperties(QObject* object);

signals:
	void layerChanged();
	void namespaceChanged();
	void keyboardFocusChanged();
	void exclusionModeChanged();

private:
	WlrLayer::Enum mLayer = WlrLayer::Top;
	QString mNamespace = QStringLiteral("quickshell");
	WlrKeyboardFocus::Enum mKeyboardFocus = WlrKeyboardFocus::None;
	ExclusionMode::Enum mExclusionMode = ExclusionMode::Auto;
};

} // namespace qs::windows::wayland
