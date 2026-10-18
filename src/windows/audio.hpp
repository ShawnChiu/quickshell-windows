#pragma once

#include <array>

#include <qobject.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/doc.hpp"
#include "../core/model.hpp"

namespace qs::windows {

///! Direction audio flows through an endpoint.
/// See @@AudioEndpoint.flow.
class AudioFlow: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		/// Plays sound, such as speakers or headphones (render).
		Output = 0,
		/// Records sound, such as a microphone (capture).
		Input = 1,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::AudioFlow::Enum flow);
};

///! State of an audio endpoint.
/// See @@AudioEndpoint.state.
class AudioEndpointState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Active = 1,
		/// Disabled in the Sound control panel or Settings.
		Disabled = 2,
		/// The adapter is gone. Such endpoints are removed from @@Audio.endpoints.
		NotPresent = 4,
		/// Nothing is plugged into the endpoint's jack, such as HDMI without a display.
		Unplugged = 8,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::AudioEndpointState::Enum state);
};

///! Physical kind of an audio endpoint.
/// See @@AudioEndpoint.formFactor.
class AudioFormFactor: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		RemoteNetworkDevice = 0,
		Speakers = 1,
		LineLevel = 2,
		Headphones = 3,
		Microphone = 4,
		Headset = 5,
		Handset = 6,
		UnknownDigitalPassthrough = 7,
		Spdif = 8,
		/// HDMI or DisplayPort, usually the display's own speakers.
		DigitalAudioDisplayDevice = 9,
		Unknown = 10,
	};
	Q_ENUM(Enum);

	Q_INVOKABLE static QString toString(qs::windows::AudioFormFactor::Enum formFactor);
};

class AudioEndpointPrivate;
class AudioService;

///! An audio endpoint device.
/// An output or input as listed in Settings > System > Sound.
/// @@volume and @@muted are only available while the endpoint is `Active`.
class AudioEndpoint: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_UNCREATABLE("Audio endpoints can only be acquired from Audio");
	// clang-format off
	/// Endpoint id, stable across reboots.
	Q_PROPERTY(QString id READ id CONSTANT);
	Q_PROPERTY(qs::windows::AudioFlow::Enum flow READ flow CONSTANT);
	/// Full name, such as `Speakers (Realtek(R) Audio)`.
	Q_PROPERTY(QString name READ default NOTIFY nameChanged BINDABLE bindableName);
	/// The endpoint's own name, such as `Speakers`, which users may rename.
	Q_PROPERTY(QString description READ default NOTIFY descriptionChanged BINDABLE bindableDescription);
	/// Name of the adapter, such as `Realtek(R) Audio`.
	Q_PROPERTY(QString interfaceName READ default NOTIFY interfaceNameChanged BINDABLE bindableInterfaceName);
	Q_PROPERTY(qs::windows::AudioFormFactor::Enum formFactor READ default NOTIFY formFactorChanged BINDABLE bindableFormFactor);
	Q_PROPERTY(qs::windows::AudioEndpointState::Enum state READ default NOTIFY stateChanged BINDABLE bindableState);
	/// Master volume from 0 to 1, on the same scale as the Windows volume slider.
	Q_PROPERTY(qreal volume READ default WRITE setVolume NOTIFY volumeChanged BINDABLE bindableVolume);
	Q_PROPERTY(bool muted READ default WRITE setMuted NOTIFY mutedChanged BINDABLE bindableMuted);
	// clang-format on

public:
	explicit AudioEndpoint(
	    AudioEndpointPrivate* impl,
	    QString id,
	    AudioFlow::Enum flow,
	    QObject* parent
	);
	~AudioEndpoint() override;
	Q_DISABLE_COPY_MOVE(AudioEndpoint);

	[[nodiscard]] QString id() const { return this->mId; }
	[[nodiscard]] AudioFlow::Enum flow() const { return this->mFlow; }

	[[nodiscard]] QBindable<QString> bindableName() const { return &this->bName; }
	[[nodiscard]] QBindable<QString> bindableDescription() const { return &this->bDescription; }
	[[nodiscard]] QBindable<QString> bindableInterfaceName() const { return &this->bInterfaceName; }
	[[nodiscard]] QBindable<AudioFormFactor::Enum> bindableFormFactor() const {
		return &this->bFormFactor;
	}
	[[nodiscard]] QBindable<AudioEndpointState::Enum> bindableState() const { return &this->bState; }
	[[nodiscard]] QBindable<qreal> bindableVolume() const { return &this->bVolume; }
	[[nodiscard]] QBindable<bool> bindableMuted() const { return &this->bMuted; }

	void setVolume(qreal volume);
	void setMuted(bool muted);

signals:
	void nameChanged();
	void descriptionChanged();
	void interfaceNameChanged();
	void formFactorChanged();
	void stateChanged();
	void volumeChanged();
	void mutedChanged();

private:
	void refreshProperties();
	void setState(AudioEndpointState::Enum state);
	void updateVolume(qreal volume, bool muted);

	AudioEndpointPrivate* impl;
	QString mId;
	AudioFlow::Enum mFlow;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(AudioEndpoint, QString, bName, &AudioEndpoint::nameChanged);
	Q_OBJECT_BINDABLE_PROPERTY(AudioEndpoint, QString, bDescription, &AudioEndpoint::descriptionChanged);
	Q_OBJECT_BINDABLE_PROPERTY(AudioEndpoint, QString, bInterfaceName, &AudioEndpoint::interfaceNameChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(AudioEndpoint, AudioFormFactor::Enum, bFormFactor, AudioFormFactor::Unknown, &AudioEndpoint::formFactorChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(AudioEndpoint, AudioEndpointState::Enum, bState, AudioEndpointState::NotPresent, &AudioEndpoint::stateChanged);
	Q_OBJECT_BINDABLE_PROPERTY(AudioEndpoint, qreal, bVolume, &AudioEndpoint::volumeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(AudioEndpoint, bool, bMuted, &AudioEndpoint::mutedChanged);
	// clang-format on

	friend class AudioService;
};

class AudioServicePrivate;

// Process wide endpoint list behind Audio, so devices are only watched once across reloads.
class AudioService: public QObject {
	Q_OBJECT;

public:
	static AudioService* instance();

	[[nodiscard]] ObjectModel<AudioEndpoint>* endpoints() { return &this->mEndpoints; }

	void setDefault(AudioEndpoint* endpoint, AudioFlow::Enum flow);

signals:
	void defaultOutputChanged();
	void defaultInputChanged();

public:
	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY(AudioService, AudioEndpoint*, bDefaultOutput, &AudioService::defaultOutputChanged);
	Q_OBJECT_BINDABLE_PROPERTY(AudioService, AudioEndpoint*, bDefaultInput, &AudioService::defaultInputChanged);
	// clang-format on

private:
	explicit AudioService();

	[[nodiscard]] AudioEndpoint* find(const QString& id) const;
	void updateEndpoint(const QString& id);
	void removeEndpoint(const QString& id);
	void updateDefault(AudioFlow::Enum flow, const QString& id);
	void updateProperties(const QString& id);
	void updateVolume(const QString& id, qreal volume, bool muted);
	void resolveDefaults();

	AudioServicePrivate* impl;
	ObjectModel<AudioEndpoint> mEndpoints {this};
	std::array<QString, 2> defaultIds;

	friend class AudioServicePrivate;
	friend class NotificationClient;
	friend class VolumeCallback;
};

///! Audio outputs and inputs.
/// Windows' view of audio: the endpoint devices of the [Core Audio APIs], their volume and the
/// default output and input.
///
/// > [!NOTE] There is no @@Quickshell.Services.Pipewire on Windows. Configs shared with Linux
/// > can load a small file per platform that reads either this or PipeWire.
///
/// [Core Audio APIs]: https://learn.microsoft.com/windows/win32/coreaudio/mmdevice-api
class AudioQml: public QObject {
	Q_OBJECT;
	QML_NAMED_ELEMENT(Audio);
	QML_SINGLETON;
	// clang-format off
	/// Outputs and inputs that are active, disabled or unplugged.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::windows::AudioEndpoint>*);
	Q_PROPERTY(UntypedObjectModel* endpoints READ endpoints CONSTANT);
	/// The default output, or null if there is none.
	///
	/// May be set to an output to make it the default, for media and communications alike as
	/// Settings does. It reads back once Windows reports the change.
	Q_PROPERTY(qs::windows::AudioEndpoint* defaultOutput READ default WRITE setDefaultOutput NOTIFY defaultOutputChanged BINDABLE bindableDefaultOutput);
	/// The default input, or null if there is none. May be set like @@defaultOutput.
	Q_PROPERTY(qs::windows::AudioEndpoint* defaultInput READ default WRITE setDefaultInput NOTIFY defaultInputChanged BINDABLE bindableDefaultInput);
	// clang-format on

public:
	explicit AudioQml(QObject* parent = nullptr);

	[[nodiscard]] static ObjectModel<AudioEndpoint>* endpoints() {
		return AudioService::instance()->endpoints();
	}

	[[nodiscard]] static QBindable<AudioEndpoint*> bindableDefaultOutput() {
		return &AudioService::instance()->bDefaultOutput;
	}

	[[nodiscard]] static QBindable<AudioEndpoint*> bindableDefaultInput() {
		return &AudioService::instance()->bDefaultInput;
	}

	static void setDefaultOutput(AudioEndpoint* endpoint) {
		AudioService::instance()->setDefault(endpoint, AudioFlow::Output);
	}

	static void setDefaultInput(AudioEndpoint* endpoint) {
		AudioService::instance()->setDefault(endpoint, AudioFlow::Input);
	}

signals:
	void defaultOutputChanged();
	void defaultInputChanged();
};

} // namespace qs::windows
