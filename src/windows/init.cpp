#include <qguiapplication.h>
#include <qlist.h>
#include <qqml.h>
#include <qstring.h>

#include "../core/plugin.hpp"
#include "../window/proxywindow.hpp"
#include "input_mask.hpp"
#include "panel_window.hpp"

namespace {

class WindowsPlugin: public QsEnginePlugin {
	QList<QString> dependencies() override { return {"window"}; }

	bool applies() override { return QGuiApplication::platformName() == "windows"; }

	void init() override {
		WinPanelWindow::installNativeEventFilter();
		ProxyWindowBase::applyInputMask = &qs::windows::applyInputMask;
	}

	void registerTypes() override {
		qmlRegisterType<WinPanelInterface>("Quickshell._WindowsOverlay", 1, 0, "PanelWindow");

		qmlRegisterModuleImport(
		    "Quickshell",
		    QQmlModuleImportModuleAny,
		    "Quickshell._WindowsOverlay",
		    QQmlModuleImportLatest
		);
	}
};

QS_REGISTER_PLUGIN(WindowsPlugin);

} // namespace
