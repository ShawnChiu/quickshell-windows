#include "core.hpp"

#include <qobject.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtypes.h>

#include "../../core/model.hpp"
#include "../power.hpp"

namespace qs::windows::upower {

QString UPowerDeviceState::toString(UPowerDeviceState::Enum status) {
	switch (status) {
	case UPowerDeviceState::Unknown: return "Unknown";
	case UPowerDeviceState::Charging: return "Charging";
	case UPowerDeviceState::Discharging: return "Discharging";
	case UPowerDeviceState::Empty: return "Empty";
	case UPowerDeviceState::FullyCharged: return "Fully Charged";
	case UPowerDeviceState::PendingCharge: return "Pending Charge";
	case UPowerDeviceState::PendingDischarge: return "Pending Discharge";
	default: return "Invalid Status";
	}
}

QString UPowerDeviceType::toString(UPowerDeviceType::Enum type) {
	switch (type) {
	case UPowerDeviceType::Unknown: return "Unknown";
	case UPowerDeviceType::LinePower: return "Line Power";
	case UPowerDeviceType::Battery: return "Battery";
	case UPowerDeviceType::Ups: return "Ups";
	case UPowerDeviceType::Monitor: return "Monitor";
	case UPowerDeviceType::Mouse: return "Mouse";
	case UPowerDeviceType::Keyboard: return "Keyboard";
	case UPowerDeviceType::Pda: return "Pda";
	case UPowerDeviceType::Phone: return "Phone";
	case UPowerDeviceType::MediaPlayer: return "Media Player";
	case UPowerDeviceType::Tablet: return "Tablet";
	case UPowerDeviceType::Computer: return "Computer";
	case UPowerDeviceType::GamingInput: return "Gaming Input";
	case UPowerDeviceType::Pen: return "Pen";
	case UPowerDeviceType::Touchpad: return "Touchpad";
	case UPowerDeviceType::Modem: return "Modem";
	case UPowerDeviceType::Network: return "Network";
	case UPowerDeviceType::Headset: return "Headset";
	case UPowerDeviceType::Speakers: return "Speakers";
	case UPowerDeviceType::Headphones: return "Headphones";
	case UPowerDeviceType::Video: return "Video";
	case UPowerDeviceType::OtherAudio: return "Other Audio";
	case UPowerDeviceType::RemoteControl: return "Remote Control";
	case UPowerDeviceType::Printer: return "Printer";
	case UPowerDeviceType::Scanner: return "Scanner";
	case UPowerDeviceType::Camera: return "Camera";
	case UPowerDeviceType::Wearable: return "Wearable";
	case UPowerDeviceType::Toy: return "Toy";
	case UPowerDeviceType::BluetoothGeneric: return "Bluetooth Generic";
	default: return "Invalid Type";
	}
}

UPowerDevice::UPowerDevice(Battery* battery, bool display, QObject* parent): QObject(parent) {
	auto* power = PowerService::instance();

	this->bIsPresent.setBinding([battery]() {
		return battery->bindableStatus().value() != BatteryStatus::NotPresent;
	});

	this->bType.setBinding([this, display]() {
		return display && !this->bIsPresent ? UPowerDeviceType::Unknown : UPowerDeviceType::Battery;
	});

	this->bPowerSupply.setBinding([this]() { return this->bType == UPowerDeviceType::Battery; });

	this->bIsLaptopBattery.setBinding([this]() {
		return this->bType == UPowerDeviceType::Battery && this->bPowerSupply;
	});

	this->bEnergy.setBinding([battery]() {
		auto remaining = battery->bindableRemainingCapacity().value();
		return remaining > 0 ? remaining / 1000.0 : 0.0;
	});

	this->bEnergyCapacity.setBinding([battery]() {
		auto full = battery->bindableFullChargeCapacity().value();
		return full > 0 ? full / 1000.0 : 0.0;
	});

	this->bChangeRate.setBinding([battery]() {
		return battery->bindableChargeRate().value() / 1000.0;
	});

	// Like UPower, 0 to 1. PowerManager's percentage covers batteries that report no capacities.
	this->bPercentage.setBinding([this, display, power]() {
		if (this->bEnergyCapacity > 0) {
			return qBound(0.0, this->bEnergy / this->bEnergyCapacity, 1.0);
		}

		if (display && this->bIsPresent) {
			return power->bRemainingChargePercent.value() / 100.0;
		}

		return 0.0;
	});

	// Idle means a charger is holding the battery, full or below a charge limit.
	this->bState.setBinding([this, battery]() {
		switch (battery->bindableStatus().value()) {
		case BatteryStatus::Charging: return UPowerDeviceState::Charging;
		case BatteryStatus::Discharging: return UPowerDeviceState::Discharging;
		case BatteryStatus::Idle:
			return qRound(this->bPercentage * 100) >= 100 ? UPowerDeviceState::FullyCharged
			                                              : UPowerDeviceState::PendingCharge;
		default: return UPowerDeviceState::Unknown;
		}
	});

	// Like UPower, 0 unless discharging. The display device takes Windows' own estimate.
	this->bTimeToEmpty.setBinding([this, display, power]() {
		if (this->bState != UPowerDeviceState::Discharging) return 0.0;

		if (display) {
			auto estimate = power->bRemainingDischargeTime.value();
			if (estimate > 0) return estimate;
		}

		return this->bChangeRate < 0 ? this->bEnergy / -this->bChangeRate * 3600 : 0.0;
	});

	this->bTimeToFull.setBinding([this]() {
		if (this->bState != UPowerDeviceState::Charging || this->bChangeRate <= 0) return 0.0;
		auto missing = this->bEnergyCapacity - this->bEnergy;
		return missing > 0 ? missing / this->bChangeRate * 3600 : 0.0;
	});

	this->bHealthPercentage.setBinding([battery]() {
		auto design = battery->bindableDesignCapacity().value();
		auto full = battery->bindableFullChargeCapacity().value();
		return design > 0 && full > 0 ? full * 100.0 / design : 0.0;
	});

	this->bHealthSupported.setBinding([this]() { return this->bHealthPercentage.value() != 0; });

	// UPower's icon names.
	this->bIconName.setBinding([this]() -> QString {
		if (this->bType != UPowerDeviceType::Battery) return "";
		if (!this->bIsPresent) return "battery-missing-symbolic";

		auto state = this->bState.value();
		if (state == UPowerDeviceState::FullyCharged) return "battery-full-charged-symbolic";
		if (state == UPowerDeviceState::Unknown) return "battery-missing-symbolic";

		auto percentage = this->bPercentage * 100;
		auto level = percentage < 10 ? "caution"
		           : percentage < 30 ? "low"
		           : percentage < 60 ? "good"
		                             : "full";

		return state == UPowerDeviceState::Charging || state == UPowerDeviceState::PendingCharge
		         ? QString("battery-%1-charging-symbolic").arg(level)
		         : QString("battery-%1-symbolic").arg(level);
	});

	this->bNativePath = battery->deviceId();
	this->bModel = battery->name();
	this->bReady.setBinding([battery]() { return battery->bindableReady().value(); });
}

UPower::UPower() {
	auto* power = PowerService::instance();

	this->mDisplayDevice = new UPowerDevice(power->aggregateBattery(), true, this);

	this->bOnBattery.setBinding([power]() {
		return power->bPowerSupplyStatus.value() == PowerSupplyStatus::NotPresent;
	});

	auto* batteries = power->batteries();

	QObject::connect(
	    batteries,
	    &UntypedObjectModel::objectInsertedPost,
	    this,
	    &UPower::onBatteryInserted
	);

	QObject::connect(
	    batteries,
	    &UntypedObjectModel::objectRemovedPre,
	    this,
	    &UPower::onBatteryRemoved
	);

	for (auto* battery: batteries->valueList()) {
		this->onBatteryInserted(battery);
	}
}

void UPower::onBatteryInserted(QObject* object) {
	auto* battery = static_cast<Battery*>(object); // NOLINT
	auto* device = new UPowerDevice(battery, false, this);
	this->mDeviceMap.insert(battery, device);
	this->mDevices.insertObject(device);
}

void UPower::onBatteryRemoved(QObject* object) {
	auto* device = this->mDeviceMap.take(static_cast<Battery*>(object)); // NOLINT
	if (!device) return;

	this->mDevices.removeObject(device);
	device->deleteLater();
}

UPower* UPower::instance() {
	static auto* instance = new UPower(); // NOLINT
	return instance;
}

UPowerQml::UPowerQml(QObject* parent): QObject(parent) {
	QObject::connect(
	    UPower::instance(),
	    &UPower::onBatteryChanged,
	    this,
	    &UPowerQml::onBatteryChanged
	);
}

} // namespace qs::windows::upower
