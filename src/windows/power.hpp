#pragma once

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"

namespace qs::windows {

///! Battery status as Windows reports it.
/// See @@PowerManager.batteryStatus and @@Battery.status.
class BatteryStatus: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		NotPresent = 0,
		Discharging = 1,
		/// Neither charging nor discharging, such as full or held below full on a charger.
		Idle = 2,
		Charging = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::BatteryStatus::Enum status);
};

///! State of the external power supply.
/// See @@PowerManager.powerSupplyStatus.
class PowerSupplyStatus: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// Running on battery.
		NotPresent = 0,
		/// Plugged in, but the charger provides less power than the system draws.
		Inadequate = 1,
		Adequate = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::PowerSupplyStatus::Enum status);
};

///! State of energy saver (battery saver on older versions of Windows).
/// See @@PowerManager.energySaverStatus.
class EnergySaverStatus: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// Energy saver cannot turn on, such as while plugged in on some systems.
		Disabled = 0,
		Off = 1,
		On = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::EnergySaverStatus::Enum status);
};

///! Power mode chosen in Windows settings.
/// See @@PowerManager.powerMode.
class PowerMode: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// The power mode could not be read, or is one Windows does not offer by default.
		Unknown = 0,
		BestPowerEfficiency = 1,
		Balanced = 2,
		BestPerformance = 3,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::PowerMode::Enum mode);
};

///! Power mode the system is actually running in.
/// See @@PowerManager.effectivePowerMode.
class EffectivePowerMode: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// Energy saver is on.
		BatterySaver = 0,
		BetterBattery = 1,
		Balanced = 2,
		HighPerformance = 3,
		MaxPerformance = 4,
		GameMode = 5,
		MixedReality = 6,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::EffectivePowerMode::Enum mode);
};

class BatteryPrivate;

///! A battery, or all batteries combined.
/// Capacities are -1 and the charge rate 0 when Windows does not report them,
/// which is always the case for @@PowerManager.aggregateBattery when there is no battery.
class Battery: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("Batteries can only be acquired from PowerManager");
	// clang-format off
	/// Device interface path of the battery, empty for @@PowerManager.aggregateBattery.
	Q_PROPERTY(QString deviceId READ deviceId CONSTANT);
	/// Name of the battery's device, empty for @@PowerManager.aggregateBattery.
	Q_PROPERTY(QString name READ name CONSTANT);
	Q_PROPERTY(qs::windows::BatteryStatus::Enum status READ default NOTIFY statusChanged BINDABLE bindableStatus);
	/// Rate of charge in milliwatts, negative when discharging.
	Q_PROPERTY(qint32 chargeRate READ default NOTIFY chargeRateChanged BINDABLE bindableChargeRate);
	/// Capacity the battery was designed for, in milliwatt-hours.
	Q_PROPERTY(qint32 designCapacity READ default NOTIFY designCapacityChanged BINDABLE bindableDesignCapacity);
	/// Capacity of the battery when fully charged, in milliwatt-hours.
	Q_PROPERTY(qint32 fullChargeCapacity READ default NOTIFY fullChargeCapacityChanged BINDABLE bindableFullChargeCapacity);
	/// Energy left in the battery, in milliwatt-hours.
	Q_PROPERTY(qint32 remainingCapacity READ default NOTIFY remainingCapacityChanged BINDABLE bindableRemainingCapacity);
	/// If the battery has reported its state yet.
	Q_PROPERTY(bool ready READ default NOTIFY readyChanged BINDABLE bindableReady);
	// clang-format on

public:
	explicit Battery(BatteryPrivate* impl, QString deviceId, QString name, QObject* parent);
	~Battery() override;
	Q_DISABLE_COPY_MOVE(Battery);

	[[nodiscard]] QString deviceId() const { return this->mDeviceId; }
	[[nodiscard]] QString name() const { return this->mName; }

	[[nodiscard]] QBindable<BatteryStatus::Enum> bindableStatus() const { return &this->bStatus; }
	[[nodiscard]] QBindable<qint32> bindableChargeRate() const { return &this->bChargeRate; }
	[[nodiscard]] QBindable<qint32> bindableDesignCapacity() const { return &this->bDesignCapacity; }
	[[nodiscard]] QBindable<qint32> bindableFullChargeCapacity() const {
		return &this->bFullChargeCapacity;
	}
	[[nodiscard]] QBindable<qint32> bindableRemainingCapacity() const {
		return &this->bRemainingCapacity;
	}
	[[nodiscard]] QBindable<bool> bindableReady() const { return &this->bReady; }

signals:
	void statusChanged();
	void chargeRateChanged();
	void designCapacityChanged();
	void fullChargeCapacityChanged();
	void remainingCapacityChanged();
	void readyChanged();

private:
	void refresh();

	BatteryPrivate* impl;
	QString mDeviceId;
	QString mName;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(Battery, BatteryStatus::Enum, bStatus, &Battery::statusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Battery, qint32, bChargeRate, &Battery::chargeRateChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(Battery, qint32, bDesignCapacity, -1, &Battery::designCapacityChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(Battery, qint32, bFullChargeCapacity, -1, &Battery::fullChargeCapacityChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(Battery, qint32, bRemainingCapacity, -1, &Battery::remainingCapacityChanged);
	Q_OBJECT_BINDABLE_PROPERTY(Battery, bool, bReady, &Battery::readyChanged);
	// clang-format on
};

class PowerServicePrivate;

// Process wide power state behind PowerManager, so it is only watched once across reloads.
class PowerService: public QObject {
	Q_OBJECT;

public:
	static PowerService* instance();

	[[nodiscard]] Battery* aggregateBattery() const { return this->mAggregateBattery; }
	[[nodiscard]] ObjectModel<Battery>* batteries() { return &this->mBatteries; }

	void setPowerMode(PowerMode::Enum mode);

signals:
	void batteryStatusChanged();
	void powerSupplyStatusChanged();
	void energySaverStatusChanged();
	void remainingChargePercentChanged();
	void remainingDischargeTimeChanged();
	void powerModeChanged();
	void effectivePowerModeChanged();

public:
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(PowerService, BatteryStatus::Enum, bBatteryStatus, &PowerService::batteryStatusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerService, PowerSupplyStatus::Enum, bPowerSupplyStatus, &PowerService::powerSupplyStatusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerService, EnergySaverStatus::Enum, bEnergySaverStatus, &PowerService::energySaverStatusChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerService, qint32, bRemainingChargePercent, &PowerService::remainingChargePercentChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(PowerService, qreal, bRemainingDischargeTime, -1, &PowerService::remainingDischargeTimeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerService, PowerMode::Enum, bPowerMode, &PowerService::powerModeChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(PowerService, EffectivePowerMode::Enum, bEffectivePowerMode, EffectivePowerMode::Balanced, &PowerService::effectivePowerModeChanged);
	// clang-format on

private:
	explicit PowerService();

	void refresh();
	void refreshPowerMode(EffectivePowerMode::Enum effective);
	void addBattery(BatteryPrivate* impl, const QString& deviceId, const QString& name);
	void removeBattery(const QString& deviceId);

	PowerServicePrivate* impl;
	Battery* mAggregateBattery = nullptr;
	ObjectModel<Battery> mBatteries {this};

	friend class PowerServicePrivate;
};

///! Power supply, batteries and power mode.
/// Windows' view of power: the [PowerManager] and [Battery] WinRT classes, plus the power mode.
/// @@Quickshell.Services.UPower is provided on top of this for configs shared with Linux.
///
/// [PowerManager]: https://learn.microsoft.com/uwp/api/windows.system.power.powermanager
/// [Battery]: https://learn.microsoft.com/uwp/api/windows.devices.power.battery
class PowerManagerQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(PowerManager);
	QML_SINGLETON;
	// clang-format off
	/// Combined status of all batteries, `NotPresent` if there are none.
	Q_PROPERTY(qs::windows::BatteryStatus::Enum batteryStatus READ default NOTIFY batteryStatusChanged BINDABLE bindableBatteryStatus);
	Q_PROPERTY(qs::windows::PowerSupplyStatus::Enum powerSupplyStatus READ default NOTIFY powerSupplyStatusChanged BINDABLE bindablePowerSupplyStatus);
	Q_PROPERTY(qs::windows::EnergySaverStatus::Enum energySaverStatus READ default NOTIFY energySaverStatusChanged BINDABLE bindableEnergySaverStatus);
	/// Combined charge of all batteries, 0 to 100. Windows reports 100 when there is no battery.
	Q_PROPERTY(qint32 remainingChargePercent READ default NOTIFY remainingChargePercentChanged BINDABLE bindableRemainingChargePercent);
	/// Estimated seconds until the batteries are empty, or -1 when unknown or not discharging.
	Q_PROPERTY(qreal remainingDischargeTime READ default NOTIFY remainingDischargeTimeChanged BINDABLE bindableRemainingDischargeTime);
	/// The power mode chosen in Windows settings (Settings > System > Power).
	///
	/// May be set to change it. Windows only applies power modes with the Balanced power plan.
	Q_PROPERTY(qs::windows::PowerMode::Enum powerMode READ default WRITE setPowerMode NOTIFY powerModeChanged BINDABLE bindablePowerMode);
	/// The power mode in effect, which also accounts for energy saver and game mode.
	Q_PROPERTY(qs::windows::EffectivePowerMode::Enum effectivePowerMode READ default NOTIFY effectivePowerModeChanged BINDABLE bindableEffectivePowerMode);
	/// All batteries combined. Never null.
	Q_PROPERTY(qs::windows::Battery* aggregateBattery READ aggregateBattery CONSTANT);
	/// Each battery in the system.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::Battery>*);
	Q_PROPERTY(UntypedObjectModel* batteries READ batteries CONSTANT);
	// clang-format on

public:
	explicit PowerManagerQml(QObject* parent = nullptr);

	[[nodiscard]] static Battery* aggregateBattery() {
		return PowerService::instance()->aggregateBattery();
	}

	[[nodiscard]] static ObjectModel<Battery>* batteries() {
		return PowerService::instance()->batteries();
	}

	static void setPowerMode(PowerMode::Enum mode) { PowerService::instance()->setPowerMode(mode); }

	[[nodiscard]] static QBindable<BatteryStatus::Enum> bindableBatteryStatus() {
		return &PowerService::instance()->bBatteryStatus;
	}

	[[nodiscard]] static QBindable<PowerSupplyStatus::Enum> bindablePowerSupplyStatus() {
		return &PowerService::instance()->bPowerSupplyStatus;
	}

	[[nodiscard]] static QBindable<EnergySaverStatus::Enum> bindableEnergySaverStatus() {
		return &PowerService::instance()->bEnergySaverStatus;
	}

	[[nodiscard]] static QBindable<qint32> bindableRemainingChargePercent() {
		return &PowerService::instance()->bRemainingChargePercent;
	}

	[[nodiscard]] static QBindable<qreal> bindableRemainingDischargeTime() {
		return &PowerService::instance()->bRemainingDischargeTime;
	}

	[[nodiscard]] static QBindable<PowerMode::Enum> bindablePowerMode() {
		return &PowerService::instance()->bPowerMode;
	}

	[[nodiscard]] static QBindable<EffectivePowerMode::Enum> bindableEffectivePowerMode() {
		return &PowerService::instance()->bEffectivePowerMode;
	}

signals:
	void batteryStatusChanged();
	void powerSupplyStatusChanged();
	void energySaverStatusChanged();
	void remainingChargePercentChanged();
	void remainingDischargeTimeChanged();
	void powerModeChanged();
	void effectivePowerModeChanged();
};

} // namespace qs::windows
