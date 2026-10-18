#include <chrono>
#include <utility>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtypes.h>
#include <windows.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Power.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.Power.h>

#include "../core/logcat.hpp"
#include "../core/model.hpp"
#include "power.hpp"

namespace wdp = winrt::Windows::Devices::Power;
namespace wde = winrt::Windows::Devices::Enumeration;
namespace wsp = winrt::Windows::System::Power;

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logPower, "quickshell.windows.power", QtWarningMsg);

QString fromHString(const winrt::hstring& string) {
	return QString::fromWCharArray(string.c_str(), static_cast<qsizetype>(string.size()));
}

qint32 valueOr(const winrt::Windows::Foundation::IReference<qint32>& value, qint32 fallback) {
	return value ? value.Value() : fallback;
}

// Power mode overlays shown in Settings > System > Power.
constexpr GUID OVERLAY_BEST_POWER_EFFICIENCY =
    {0x961cc777, 0x2547, 0x4f9d, {0x81, 0x74, 0x7d, 0x86, 0x18, 0x1b, 0x8a, 0x7a}};
constexpr GUID OVERLAY_BALANCED = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
constexpr GUID OVERLAY_BEST_PERFORMANCE =
    {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};

// Undocumented powrprof exports behind the power mode setting.
using PowerGetActualOverlaySchemeFn = DWORD(WINAPI*)(GUID*);
using PowerSetActiveOverlaySchemeFn = DWORD(WINAPI*)(GUID);

// powersetting.h only declares these from NTDDI_WIN10_RS5, above what this builds for.
using EffectivePowerModeCallback = void(WINAPI*)(qint32 mode, void* context);
using PowerRegisterForEffectivePowerModeNotificationsFn = HRESULT(WINAPI*)(
    ULONG version,
    EffectivePowerModeCallback callback,
    void* context,
    void** handle
);
constexpr ULONG EFFECTIVE_POWER_MODE_V2 = 2;

FARPROC powrprofProc(const char* name) {
	static HMODULE module = LoadLibraryW(L"powrprof.dll");
	return module ? GetProcAddress(module, name) : nullptr;
}

} // namespace

QString BatteryStatus::toString(BatteryStatus::Enum status) {
	switch (status) {
	case BatteryStatus::NotPresent: return "Not Present";
	case BatteryStatus::Discharging: return "Discharging";
	case BatteryStatus::Idle: return "Idle";
	case BatteryStatus::Charging: return "Charging";
	default: return "Invalid Status";
	}
}

QString PowerSupplyStatus::toString(PowerSupplyStatus::Enum status) {
	switch (status) {
	case PowerSupplyStatus::NotPresent: return "Not Present";
	case PowerSupplyStatus::Inadequate: return "Inadequate";
	case PowerSupplyStatus::Adequate: return "Adequate";
	default: return "Invalid Status";
	}
}

QString EnergySaverStatus::toString(EnergySaverStatus::Enum status) {
	switch (status) {
	case EnergySaverStatus::Disabled: return "Disabled";
	case EnergySaverStatus::Off: return "Off";
	case EnergySaverStatus::On: return "On";
	default: return "Invalid Status";
	}
}

QString PowerMode::toString(PowerMode::Enum mode) {
	switch (mode) {
	case PowerMode::Unknown: return "Unknown";
	case PowerMode::BestPowerEfficiency: return "Best Power Efficiency";
	case PowerMode::Balanced: return "Balanced";
	case PowerMode::BestPerformance: return "Best Performance";
	default: return "Invalid Mode";
	}
}

QString EffectivePowerMode::toString(EffectivePowerMode::Enum mode) {
	switch (mode) {
	case EffectivePowerMode::BatterySaver: return "Battery Saver";
	case EffectivePowerMode::BetterBattery: return "Better Battery";
	case EffectivePowerMode::Balanced: return "Balanced";
	case EffectivePowerMode::HighPerformance: return "High Performance";
	case EffectivePowerMode::MaxPerformance: return "Max Performance";
	case EffectivePowerMode::GameMode: return "Game Mode";
	case EffectivePowerMode::MixedReality: return "Mixed Reality";
	default: return "Invalid Mode";
	}
}

class BatteryPrivate {
public:
	explicit BatteryPrivate(wdp::Battery battery): battery(std::move(battery)) {}

	wdp::Battery battery;
	wdp::Battery::ReportUpdated_revoker reportUpdated;
};

Battery::Battery(BatteryPrivate* impl, QString deviceId, QString name, QObject* parent)
    : QObject(parent)
    , impl(impl)
    , mDeviceId(std::move(deviceId))
    , mName(std::move(name)) {
	if (!this->impl->battery) return;

	// Reports arrive on the thread pool.
	auto self = QPointer(this);
	try {
		this->impl->reportUpdated =
		    this->impl->battery.ReportUpdated(winrt::auto_revoke, [self](auto&&, auto&&) {
			    QMetaObject::invokeMethod(
			        QCoreApplication::instance(),
			        [self]() {
				        if (self) self->refresh();
			        },
			        Qt::QueuedConnection
			    );
		    });
	} catch (const winrt::hresult_error& e) {
		qCWarning(logPower) << "Could not watch battery" << this->mDeviceId << fromHString(e.message());
	}

	this->refresh();
}

Battery::~Battery() { delete this->impl; }

void Battery::refresh() {
	wdp::BatteryReport report {nullptr};
	try {
		report = this->impl->battery.GetReport();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logPower) << "Could not read battery" << this->mDeviceId << fromHString(e.message());
		return;
	}

	Qt::beginPropertyUpdateGroup();
	this->bStatus = static_cast<BatteryStatus::Enum>(report.Status());
	this->bChargeRate = valueOr(report.ChargeRateInMilliwatts(), 0);
	this->bDesignCapacity = valueOr(report.DesignCapacityInMilliwattHours(), -1);
	this->bFullChargeCapacity = valueOr(report.FullChargeCapacityInMilliwattHours(), -1);
	this->bRemainingCapacity = valueOr(report.RemainingCapacityInMilliwattHours(), -1);
	this->bReady = true;
	Qt::endPropertyUpdateGroup();
}

class PowerServicePrivate {
public:
	static void WINAPI onEffectivePowerMode(qint32 mode, void* context) {
		auto* service = static_cast<PowerService*>(context);
		QMetaObject::invokeMethod(
		    service,
		    [service, mode]() {
			    service->refreshPowerMode(static_cast<EffectivePowerMode::Enum>(mode));
		    },
		    Qt::QueuedConnection
		);
	}

	wsp::PowerManager::BatteryStatusChanged_revoker batteryStatusChanged;
	wsp::PowerManager::PowerSupplyStatusChanged_revoker powerSupplyStatusChanged;
	wsp::PowerManager::EnergySaverStatusChanged_revoker energySaverStatusChanged;
	wsp::PowerManager::RemainingChargePercentChanged_revoker remainingChargePercentChanged;
	wsp::PowerManager::RemainingDischargeTimeChanged_revoker remainingDischargeTimeChanged;
	wde::DeviceWatcher watcher {nullptr};
	void* powerModeHandle = nullptr;
};

PowerService::PowerService(): impl(new PowerServicePrivate()) {
	auto aggregate = wdp::Battery(nullptr);
	try {
		aggregate = wdp::Battery::AggregateBattery();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logPower) << "Could not read the aggregate battery:" << fromHString(e.message());
	}

	this->mAggregateBattery = new Battery(new BatteryPrivate(aggregate), "", "", this);

	// PowerManager's events arrive on the thread pool.
	auto post = [this](auto&&, auto&&) {
		QMetaObject::invokeMethod(this, [this]() { this->refresh(); }, Qt::QueuedConnection);
	};

	try {
		this->impl->batteryStatusChanged =
		    wsp::PowerManager::BatteryStatusChanged(winrt::auto_revoke, post);
		this->impl->powerSupplyStatusChanged =
		    wsp::PowerManager::PowerSupplyStatusChanged(winrt::auto_revoke, post);
		this->impl->energySaverStatusChanged =
		    wsp::PowerManager::EnergySaverStatusChanged(winrt::auto_revoke, post);
		this->impl->remainingChargePercentChanged =
		    wsp::PowerManager::RemainingChargePercentChanged(winrt::auto_revoke, post);
		this->impl->remainingDischargeTimeChanged =
		    wsp::PowerManager::RemainingDischargeTimeChanged(winrt::auto_revoke, post);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logPower) << "Could not watch power state:" << fromHString(e.message());
	}

	try {
		auto watcher = wde::DeviceInformation::CreateWatcher(wdp::Battery::GetDeviceSelector());

		watcher.Added([this](auto&&, const wde::DeviceInformation& info) {
			auto deviceId = fromHString(info.Id());
			auto name = fromHString(info.Name());

			wdp::Battery::FromIdAsync(info.Id()).Completed(
			    [this, deviceId, name](const auto& op, winrt::Windows::Foundation::AsyncStatus status) {
				    if (status != winrt::Windows::Foundation::AsyncStatus::Completed) return;
				    auto battery = op.GetResults();
				    if (!battery) return;

				    auto* impl = new BatteryPrivate(battery);
				    QMetaObject::invokeMethod(
				        this,
				        [this, impl, deviceId, name]() { this->addBattery(impl, deviceId, name); },
				        Qt::QueuedConnection
				    );
			    }
			);
		});

		// Removals are only reported while updates are watched too.
		watcher.Updated([](auto&&, auto&&) {});

		watcher.Removed([this](auto&&, const wde::DeviceInformationUpdate& update) {
			auto deviceId = fromHString(update.Id());
			QMetaObject::invokeMethod(
			    this,
			    [this, deviceId]() { this->removeBattery(deviceId); },
			    Qt::QueuedConnection
			);
		});

		watcher.Start();
		this->impl->watcher = watcher;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logPower) << "Could not enumerate batteries:" << fromHString(e.message());
	}

	// Called right away with the current mode, then on each change, including power mode changes.
	auto* registerPowerMode = reinterpret_cast<PowerRegisterForEffectivePowerModeNotificationsFn>(
	    powrprofProc("PowerRegisterForEffectivePowerModeNotifications")
	);

	auto result = registerPowerMode ? registerPowerMode(
	                                      EFFECTIVE_POWER_MODE_V2,
	                                      &PowerServicePrivate::onEffectivePowerMode,
	                                      this,
	                                      &this->impl->powerModeHandle
	                                  )
	                                : E_NOTIMPL;

	if (FAILED(result)) {
		qCWarning(logPower) << "Could not watch the power mode:" << Qt::hex
		                    << static_cast<quint32>(result);
	}

	this->refresh();
	this->refreshPowerMode(this->bEffectivePowerMode);
}

PowerService* PowerService::instance() {
	static auto* instance = new PowerService(); // NOLINT
	return instance;
}

void PowerService::refresh() {
	auto batteryStatus = wsp::BatteryStatus::NotPresent;
	auto powerSupplyStatus = wsp::PowerSupplyStatus::Adequate;
	auto energySaverStatus = wsp::EnergySaverStatus::Disabled;
	qint32 remainingChargePercent = 0;
	auto remainingDischargeTime = winrt::Windows::Foundation::TimeSpan::max();

	try {
		batteryStatus = wsp::PowerManager::BatteryStatus();
		powerSupplyStatus = wsp::PowerManager::PowerSupplyStatus();
		energySaverStatus = wsp::PowerManager::EnergySaverStatus();
		remainingChargePercent = wsp::PowerManager::RemainingChargePercent();
		remainingDischargeTime = wsp::PowerManager::RemainingDischargeTime();
	} catch (const winrt::hresult_error& e) {
		qCWarning(logPower) << "Could not read power state:" << fromHString(e.message());
		return;
	}

	Qt::beginPropertyUpdateGroup();
	this->bBatteryStatus = static_cast<BatteryStatus::Enum>(batteryStatus);
	this->bPowerSupplyStatus = static_cast<PowerSupplyStatus::Enum>(powerSupplyStatus);
	this->bEnergySaverStatus = static_cast<EnergySaverStatus::Enum>(energySaverStatus);
	this->bRemainingChargePercent = remainingChargePercent;
	// TimeSpan's maximum when unknown.
	this->bRemainingDischargeTime =
	    remainingDischargeTime == winrt::Windows::Foundation::TimeSpan::max()
	        ? -1
	        : std::chrono::duration<qreal>(remainingDischargeTime).count();
	Qt::endPropertyUpdateGroup();
}

void PowerService::refreshPowerMode(EffectivePowerMode::Enum effective) {
	this->bEffectivePowerMode = effective;

	auto* getOverlay =
	    reinterpret_cast<PowerGetActualOverlaySchemeFn>(powrprofProc("PowerGetActualOverlayScheme"));

	GUID overlay {};
	if (!getOverlay || getOverlay(&overlay) != ERROR_SUCCESS) {
		this->bPowerMode = PowerMode::Unknown;
	} else if (overlay == OVERLAY_BEST_POWER_EFFICIENCY) {
		this->bPowerMode = PowerMode::BestPowerEfficiency;
	} else if (overlay == OVERLAY_BALANCED) {
		this->bPowerMode = PowerMode::Balanced;
	} else if (overlay == OVERLAY_BEST_PERFORMANCE) {
		this->bPowerMode = PowerMode::BestPerformance;
	} else {
		this->bPowerMode = PowerMode::Unknown;
	}
}

void PowerService::setPowerMode(PowerMode::Enum mode) {
	GUID overlay {};
	switch (mode) {
	case PowerMode::BestPowerEfficiency: overlay = OVERLAY_BEST_POWER_EFFICIENCY; break;
	case PowerMode::Balanced: overlay = OVERLAY_BALANCED; break;
	case PowerMode::BestPerformance: overlay = OVERLAY_BEST_PERFORMANCE; break;
	default: qCWarning(logPower) << "Cannot set power mode to" << mode; return;
	}

	auto* setOverlay =
	    reinterpret_cast<PowerSetActiveOverlaySchemeFn>(powrprofProc("PowerSetActiveOverlayScheme"));

	if (!setOverlay) {
		qCWarning(logPower) << "Cannot set the power mode on this version of Windows.";
		return;
	}

	auto result = setOverlay(overlay);
	if (result != ERROR_SUCCESS) {
		qCWarning(logPower) << "Could not set power mode to" << mode << "error" << result;
	}

	// The effective mode, and so its notification, may not change.
	this->refreshPowerMode(this->bEffectivePowerMode);
}

void PowerService::addBattery(BatteryPrivate* impl, const QString& deviceId, const QString& name) {
	for (auto* battery: this->mBatteries.valueList()) {
		if (battery->deviceId() == deviceId) {
			delete impl;
			return;
		}
	}

	this->mBatteries.insertObject(new Battery(impl, deviceId, name, this));
}

void PowerService::removeBattery(const QString& deviceId) {
	for (auto* battery: this->mBatteries.valueList()) {
		if (battery->deviceId() == deviceId) {
			this->mBatteries.removeObject(battery);
			battery->deleteLater();
			return;
		}
	}
}

PowerManagerQml::PowerManagerQml(QObject* parent): QObject(parent) {
	auto* service = PowerService::instance();

	// clang-format off
	QObject::connect(service, &PowerService::batteryStatusChanged, this, &PowerManagerQml::batteryStatusChanged);
	QObject::connect(service, &PowerService::powerSupplyStatusChanged, this, &PowerManagerQml::powerSupplyStatusChanged);
	QObject::connect(service, &PowerService::energySaverStatusChanged, this, &PowerManagerQml::energySaverStatusChanged);
	QObject::connect(service, &PowerService::remainingChargePercentChanged, this, &PowerManagerQml::remainingChargePercentChanged);
	QObject::connect(service, &PowerService::remainingDischargeTimeChanged, this, &PowerManagerQml::remainingDischargeTimeChanged);
	QObject::connect(service, &PowerService::powerModeChanged, this, &PowerManagerQml::powerModeChanged);
	QObject::connect(service, &PowerService::effectivePowerModeChanged, this, &PowerManagerQml::effectivePowerModeChanged);
	// clang-format on
}

} // namespace qs::windows
