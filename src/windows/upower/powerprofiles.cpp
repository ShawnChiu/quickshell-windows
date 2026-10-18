#include "powerprofiles.hpp"

#include <qobject.h>
#include <qstring.h>

#include "../power.hpp"

namespace qs::windows::upower {

QString PowerProfile::toString(PowerProfile::Enum profile) {
	switch (profile) {
	case PowerProfile::PowerSaver: return "PowerSaver";
	case PowerProfile::Balanced: return "Balanced";
	case PowerProfile::Performance: return "Performance";
	default: return "Invalid";
	}
}

QString PerformanceDegradationReason::toString(PerformanceDegradationReason::Enum reason) {
	switch (reason) {
	case PerformanceDegradationReason::None: return "None";
	case PerformanceDegradationReason::LapDetected: return "LapDetected";
	case PerformanceDegradationReason::HighTemperature: return "HighTemperature";
	default: return "Invalid";
	}
}

PowerProfilesQml::PowerProfilesQml(QObject* parent): QObject(parent) {
	auto* power = PowerService::instance();

	this->bProfile.setBinding([power]() {
		switch (power->bPowerMode.value()) {
		case PowerMode::BestPowerEfficiency: return PowerProfile::PowerSaver;
		case PowerMode::BestPerformance: return PowerProfile::Performance;
		default: return PowerProfile::Balanced;
		}
	});

	this->bHasPerformanceProfile.setBinding([power]() {
		return power->bPowerMode.value() != PowerMode::Unknown;
	});
}

void PowerProfilesQml::setProfile(PowerProfile::Enum profile) {
	switch (profile) {
	case PowerProfile::PowerSaver:
		PowerService::instance()->setPowerMode(PowerMode::BestPowerEfficiency);
		break;
	case PowerProfile::Balanced: PowerService::instance()->setPowerMode(PowerMode::Balanced); break;
	case PowerProfile::Performance:
		PowerService::instance()->setPowerMode(PowerMode::BestPerformance);
		break;
	}
}

} // namespace qs::windows::upower
