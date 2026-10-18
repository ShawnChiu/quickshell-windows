#pragma once

#include <utility>

#include <qcontainerfwd.h>
#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../power.hpp"

// PowerProfiles on top of Windows' power mode. Mirrors src/services/upower/powerprofiles.hpp.
namespace qs::windows::upower {

///! Power profile exposed by the PowerProfiles service.
/// See @@PowerProfiles.
class PowerProfile: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// Windows' Best Power Efficiency mode.
		PowerSaver = 0,
		/// Windows' Balanced mode.
		Balanced = 1,
		/// Windows' Best Performance mode.
		Performance = 2,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::upower::PowerProfile::Enum profile);
};

///! Reason for performance degradation exposed by the PowerProfiles service.
/// Windows does not report one, so this is always `None`.
class PerformanceDegradationReason: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		LapDetected = 1,
		HighTemperature = 2,
	};
	Q_ENUM(Enum);

	// clang-format off
	Q_INVOKABLE static QString toString(qs::windows::upower::PerformanceDegradationReason::Enum reason);
	// clang-format on
};

class PowerProfileHold {
	Q_GADGET;
	QML_VALUE_TYPE(powerProfileHold);
	Q_PROPERTY(qs::windows::upower::PowerProfile::Enum profile MEMBER profile CONSTANT);
	Q_PROPERTY(QString applicationId MEMBER applicationId CONSTANT);
	Q_PROPERTY(QString reason MEMBER reason CONSTANT);

public:
	explicit PowerProfileHold() = default;
	explicit PowerProfileHold(PowerProfile::Enum profile, QString applicationId, QString reason)
	    : profile(profile)
	    , applicationId(std::move(applicationId))
	    , reason(std::move(reason)) {}

	PowerProfile::Enum profile = PowerProfile::Balanced;
	QString applicationId;
	QString reason;

	[[nodiscard]] bool operator==(const PowerProfileHold& other) const = default;
};

///! Provides access to the Power Profiles service.
/// On Windows, the power mode in Settings > System > Power,
/// see @@Quickshell.Windows.PowerManager.powerMode.
class PowerProfilesQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(PowerProfiles);
	QML_SINGLETON;
	// clang-format off
	/// The current power profile.
	///
	/// This property may be set to change the system's power mode.
	/// Windows only applies power modes with the Balanced power plan.
	Q_PROPERTY(qs::windows::upower::PowerProfile::Enum profile READ default WRITE setProfile NOTIFY profileChanged BINDABLE bindableProfile);
	/// If the power mode can be read, and so set.
	Q_PROPERTY(bool hasPerformanceProfile READ default NOTIFY hasPerformanceProfileChanged BINDABLE bindableHasPerformanceProfile);
	/// Always `None`.
	Q_PROPERTY(qs::windows::upower::PerformanceDegradationReason::Enum degradationReason READ default NOTIFY degradationReasonChanged BINDABLE bindableDegradationReason);
	/// Always empty, Windows has no profile holds.
	Q_PROPERTY(QList<qs::windows::upower::PowerProfileHold> holds READ default NOTIFY holdsChanged BINDABLE bindableHolds);
	// clang-format on

signals:
	void profileChanged();
	void hasPerformanceProfileChanged();
	void degradationReasonChanged();
	void holdsChanged();

public:
	explicit PowerProfilesQml(QObject* parent = nullptr);

	[[nodiscard]] QBindable<PowerProfile::Enum> bindableProfile() const { return &this->bProfile; }

	static void setProfile(PowerProfile::Enum profile);

	[[nodiscard]] QBindable<bool> bindableHasPerformanceProfile() const {
		return &this->bHasPerformanceProfile;
	}

	[[nodiscard]] QBindable<PerformanceDegradationReason::Enum> bindableDegradationReason() const {
		return &this->bDegradationReason;
	}

	[[nodiscard]] QBindable<QList<PowerProfileHold>> bindableHolds() const { return &this->bHolds; }

private:
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, PowerProfile::Enum, bProfile, &PowerProfilesQml::profileChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, bool, bHasPerformanceProfile, &PowerProfilesQml::hasPerformanceProfileChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, PerformanceDegradationReason::Enum, bDegradationReason, &PowerProfilesQml::degradationReasonChanged);
	Q_OBJECT_BINDABLE_PROPERTY(PowerProfilesQml, QList<PowerProfileHold>, bHolds, &PowerProfilesQml::holdsChanged);
	// clang-format on
};

} // namespace qs::windows::upower
