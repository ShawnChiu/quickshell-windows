#pragma once

#include <qcontainerfwd.h>
#include <qfunctionpointer.h>
#include <qlist.h>

class EngineGeneration;

class QsEnginePlugin {
public:
	QsEnginePlugin() = default;
	virtual ~QsEnginePlugin() = default;
	QsEnginePlugin(QsEnginePlugin&&) = delete;
	QsEnginePlugin(const QsEnginePlugin&) = delete;
	void operator=(QsEnginePlugin&&) = delete;
	void operator=(const QsEnginePlugin&) = delete;

	virtual QString name() { return QString(); }
	virtual QList<QString> dependencies() { return {}; }
	virtual bool applies() { return true; }
	virtual void preinit() {}
	virtual void init() {}
	virtual void registerTypes() {}
	virtual void constructGeneration(EngineGeneration& /*unused*/) {} // NOLINT
	virtual void onReload() {}

	static void registerPlugin(QsEnginePlugin& plugin);
	static void preinitPluginsOnly();
	static void initPlugins();
	static void runConstructGeneration(EngineGeneration& generation);
	static void runOnReload();
};

// NOLINTBEGIN
#if defined(_MSC_VER) && !defined(__clang__)
// MSVC has no constructor attribute, so registration runs from a static object's constructor.
#define QS_REGISTER_PLUGIN(clazz)                                                                  \
	struct QsPluginRegistrar {                                                                       \
		QsPluginRegistrar() {                                                                          \
			static clazz plugin;                                                                         \
			QsEnginePlugin::registerPlugin(plugin);                                                      \
		}                                                                                              \
	};                                                                                               \
	[[maybe_unused]] const QsPluginRegistrar qsPluginRegistrar;
#else
#define QS_REGISTER_PLUGIN(clazz)                                                                  \
	[[gnu::constructor]] void qsInitPlugin() {                                                       \
		static clazz plugin;                                                                           \
		QsEnginePlugin::registerPlugin(plugin);                                                        \
	}
#endif
// NOLINTEND
