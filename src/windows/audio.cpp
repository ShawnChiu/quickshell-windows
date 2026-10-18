#include <algorithm>
#include <utility>

#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qproperty.h>
#include <qstring.h>
#include <qtypes.h>
#include <windows.h>
#include <winrt/base.h>

#include "../core/logcat.hpp"
#include "../core/model.hpp"
#include "audio.hpp"

namespace qs::windows {

namespace {

QS_LOGGING_CATEGORY(logAudio, "quickshell.windows.audio", QtWarningMsg);

// Endpoints worth listing. NotPresent ones are adapters long gone, often dozens of them.
constexpr DWORD LISTED_STATES =
    DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED;

// The SDK's keys need a library or INITGUID.
constexpr PROPERTYKEY KEY_DEVICE_FRIENDLY_NAME = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14
};

constexpr PROPERTYKEY KEY_DEVICE_DESC = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    2
};

constexpr PROPERTYKEY KEY_INTERFACE_FRIENDLY_NAME = {
    {0x026e516e, 0xb814, 0x414b, {0x83, 0xcd, 0x85, 0x6d, 0x6f, 0xef, 0x48, 0x22}},
    2
};

constexpr PROPERTYKEY KEY_FORM_FACTOR = {
    {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}},
    0
};

bool sameKey(const PROPERTYKEY& a, const PROPERTYKEY& b) {
	return a.pid == b.pid && a.fmtid == b.fmtid;
}

// Undocumented interface behind Settings' output and input pickers, also used by EarTrumpet.
// Only SetDefaultEndpoint is called; the methods before it lay out the vtable.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig: public IUnknown {
	virtual HRESULT STDMETHODCALLTYPE GetMixFormat() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat() = 0;
	virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetShareMode() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetShareMode() = 0;
	virtual HRESULT STDMETHODCALLTYPE GetPropertyValue() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetPropertyValue() = 0;
	virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR deviceId, ERole role) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility() = 0;
};

constexpr CLSID CLSID_POLICY_CONFIG_CLIENT =
    {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

QString readString(IPropertyStore* store, const PROPERTYKEY& key) {
	PROPVARIANT value;
	PropVariantInit(&value);
	if (FAILED(store->GetValue(key, &value))) return QString();

	auto string = value.vt == VT_LPWSTR ? QString::fromWCharArray(value.pwszVal) : QString();
	PropVariantClear(&value);
	return string;
}

AudioFormFactor::Enum readFormFactor(IPropertyStore* store) {
	PROPVARIANT value;
	PropVariantInit(&value);
	if (FAILED(store->GetValue(KEY_FORM_FACTOR, &value))) return AudioFormFactor::Unknown;

	auto formFactor = AudioFormFactor::Unknown;
	if (value.vt == VT_UI4 && value.ulVal <= AudioFormFactor::Unknown) {
		formFactor = static_cast<AudioFormFactor::Enum>(value.ulVal);
	}

	PropVariantClear(&value);
	return formFactor;
}

} // namespace

QString AudioFlow::toString(AudioFlow::Enum flow) {
	switch (flow) {
	case AudioFlow::Output: return "Output";
	case AudioFlow::Input: return "Input";
	default: return "Invalid Flow";
	}
}

QString AudioEndpointState::toString(AudioEndpointState::Enum state) {
	switch (state) {
	case AudioEndpointState::Active: return "Active";
	case AudioEndpointState::Disabled: return "Disabled";
	case AudioEndpointState::NotPresent: return "Not Present";
	case AudioEndpointState::Unplugged: return "Unplugged";
	default: return "Invalid State";
	}
}

QString AudioFormFactor::toString(AudioFormFactor::Enum formFactor) {
	switch (formFactor) {
	case AudioFormFactor::RemoteNetworkDevice: return "Remote Network Device";
	case AudioFormFactor::Speakers: return "Speakers";
	case AudioFormFactor::LineLevel: return "Line Level";
	case AudioFormFactor::Headphones: return "Headphones";
	case AudioFormFactor::Microphone: return "Microphone";
	case AudioFormFactor::Headset: return "Headset";
	case AudioFormFactor::Handset: return "Handset";
	case AudioFormFactor::UnknownDigitalPassthrough: return "Unknown Digital Passthrough";
	case AudioFormFactor::Spdif: return "S/PDIF";
	case AudioFormFactor::DigitalAudioDisplayDevice: return "Digital Audio Display Device";
	case AudioFormFactor::Unknown: return "Unknown";
	default: return "Invalid Form Factor";
	}
}

// Core Audio calls back on its own threads, so callbacks only carry ids to the service's thread.
class NotificationClient: public winrt::implements<NotificationClient, IMMNotificationClient> {
public:
	explicit NotificationClient(AudioService* service): service(service) {}

	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD /*state*/) noexcept override {
		this->post([id = QString::fromWCharArray(id)](AudioService* service) {
			service->updateEndpoint(id);
		});
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR id) noexcept override {
		return this->OnDeviceStateChanged(id, 0);
	}

	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) noexcept override {
		this->post([id = QString::fromWCharArray(id)](AudioService* service) {
			service->removeEndpoint(id);
		});
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR id) noexcept override {
		// Communications may have its own default; the console default is the one shown everywhere.
		if (role != eConsole || (flow != eRender && flow != eCapture)) return S_OK;

		auto qflow = flow == eRender ? AudioFlow::Output : AudioFlow::Input;
		this->post([qflow, id = id ? QString::fromWCharArray(id) : QString()](AudioService* service) {
			service->updateDefault(qflow, id);
		});
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE
	OnPropertyValueChanged(LPCWSTR id, const PROPERTYKEY key) noexcept override {
		// Many other properties change all the time.
		if (!sameKey(key, KEY_DEVICE_FRIENDLY_NAME) && !sameKey(key, KEY_DEVICE_DESC)
		    && !sameKey(key, KEY_INTERFACE_FRIENDLY_NAME) && !sameKey(key, KEY_FORM_FACTOR))
		{
			return S_OK;
		}

		this->post([id = QString::fromWCharArray(id)](AudioService* service) {
			service->updateProperties(id);
		});
		return S_OK;
	}

private:
	template <typename F>
	void post(F callback) {
		auto* service = this->service;
		QMetaObject::invokeMethod(
		    service,
		    [service, callback = std::move(callback)]() { callback(service); },
		    Qt::QueuedConnection
		);
	}

	AudioService* service;
};

class VolumeCallback: public winrt::implements<VolumeCallback, IAudioEndpointVolumeCallback> {
public:
	VolumeCallback(AudioService* service, QString id): service(service), id(std::move(id)) {}

	HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) noexcept override {
		auto* service = this->service;
		auto volume = static_cast<qreal>(data->fMasterVolume);
		auto muted = data->bMuted != FALSE;

		QMetaObject::invokeMethod(
		    service,
		    [service, id = this->id, volume, muted]() { service->updateVolume(id, volume, muted); },
		    Qt::QueuedConnection
		);

		return S_OK;
	}

private:
	AudioService* service;
	QString id;
};

class AudioEndpointPrivate {
public:
	AudioEndpointPrivate(AudioService* service, winrt::com_ptr<IMMDevice> device)
	    : service(service)
	    , device(std::move(device)) {}

	void releaseVolume() {
		if (this->volume && this->callback) {
			this->volume->UnregisterControlChangeNotify(this->callback.get());
		}

		this->volume = nullptr;
		this->callback = nullptr;
	}

	AudioService* service;
	winrt::com_ptr<IMMDevice> device;
	winrt::com_ptr<IAudioEndpointVolume> volume;
	winrt::com_ptr<VolumeCallback> callback;
};

AudioEndpoint::AudioEndpoint(
    AudioEndpointPrivate* impl,
    QString id,
    AudioFlow::Enum flow,
    QObject* parent
)
    : QObject(parent)
    , impl(impl)
    , mId(std::move(id))
    , mFlow(flow) {
	this->refreshProperties();
}

AudioEndpoint::~AudioEndpoint() {
	this->impl->releaseVolume();
	delete this->impl;
}

void AudioEndpoint::refreshProperties() {
	winrt::com_ptr<IPropertyStore> store;
	auto result = this->impl->device->OpenPropertyStore(STGM_READ, store.put());
	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not read endpoint" << this->mId << "error" << Qt::hex
		                    << static_cast<quint32>(result);
		return;
	}

	Qt::beginPropertyUpdateGroup();
	this->bName = readString(store.get(), KEY_DEVICE_FRIENDLY_NAME);
	this->bDescription = readString(store.get(), KEY_DEVICE_DESC);
	this->bInterfaceName = readString(store.get(), KEY_INTERFACE_FRIENDLY_NAME);
	this->bFormFactor = readFormFactor(store.get());
	Qt::endPropertyUpdateGroup();
}

void AudioEndpoint::setState(AudioEndpointState::Enum state) {
	if (state == this->bState) return;

	// The volume control can only be reached while the endpoint is active.
	if (state != AudioEndpointState::Active) {
		this->impl->releaseVolume();
		Qt::beginPropertyUpdateGroup();
		this->bState = state;
		this->bVolume = 0;
		this->bMuted = false;
		Qt::endPropertyUpdateGroup();
		return;
	}

	winrt::com_ptr<IAudioEndpointVolume> volume;
	auto result =
	    this->impl->device
	        ->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, volume.put_void());

	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not reach the volume of endpoint" << this->mId << "error"
		                    << Qt::hex << static_cast<quint32>(result);
	} else {
		auto callback = winrt::make_self<VolumeCallback>(this->impl->service, this->mId);
		result = volume->RegisterControlChangeNotify(callback.get());

		if (FAILED(result)) {
			qCWarning(logAudio) << "Could not watch the volume of endpoint" << this->mId << "error"
			                    << Qt::hex << static_cast<quint32>(result);
		} else {
			this->impl->callback = std::move(callback);
		}

		this->impl->volume = std::move(volume);
	}

	float level = 0;
	BOOL muted = FALSE;
	if (this->impl->volume) {
		this->impl->volume->GetMasterVolumeLevelScalar(&level);
		this->impl->volume->GetMute(&muted);
	}

	Qt::beginPropertyUpdateGroup();
	this->bState = state;
	this->bVolume = level;
	this->bMuted = muted != FALSE;
	Qt::endPropertyUpdateGroup();
}

void AudioEndpoint::updateVolume(qreal volume, bool muted) {
	Qt::beginPropertyUpdateGroup();
	this->bVolume = volume;
	this->bMuted = muted;
	Qt::endPropertyUpdateGroup();
}

void AudioEndpoint::setVolume(qreal volume) {
	if (!this->impl->volume) {
		qCWarning(logAudio) << "Cannot set the volume of inactive endpoint" << this;
		return;
	}

	// Read back through the change notification, as changes from elsewhere are.
	auto result = this->impl->volume->SetMasterVolumeLevelScalar(
	    static_cast<float>(std::clamp(volume, 0.0, 1.0)),
	    nullptr
	);

	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not set the volume of" << this << "error" << Qt::hex
		                    << static_cast<quint32>(result);
	}
}

void AudioEndpoint::setMuted(bool muted) {
	if (!this->impl->volume) {
		qCWarning(logAudio) << "Cannot mute inactive endpoint" << this;
		return;
	}

	auto result = this->impl->volume->SetMute(muted ? TRUE : FALSE, nullptr);

	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not mute" << this << "error" << Qt::hex
		                    << static_cast<quint32>(result);
	}
}

class AudioServicePrivate {
public:
	winrt::com_ptr<IMMDeviceEnumerator> enumerator;
	winrt::com_ptr<NotificationClient> client;
};

AudioService::AudioService(): impl(new AudioServicePrivate()) {
	auto result = CoCreateInstance(
	    __uuidof(MMDeviceEnumerator),
	    nullptr,
	    CLSCTX_ALL,
	    __uuidof(IMMDeviceEnumerator),
	    this->impl->enumerator.put_void()
	);

	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not reach Windows audio, error" << Qt::hex
		                    << static_cast<quint32>(result);
		return;
	}

	// Registered first so nothing is missed between listing and watching.
	this->impl->client = winrt::make_self<NotificationClient>(this);
	result = this->impl->enumerator->RegisterEndpointNotificationCallback(this->impl->client.get());
	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not watch audio endpoints, error" << Qt::hex
		                    << static_cast<quint32>(result);
	}

	winrt::com_ptr<IMMDeviceCollection> collection;
	result = this->impl->enumerator->EnumAudioEndpoints(eAll, LISTED_STATES, collection.put());

	UINT count = 0;
	if (FAILED(result) || FAILED(collection->GetCount(&count))) {
		qCWarning(logAudio) << "Could not list audio endpoints, error" << Qt::hex
		                    << static_cast<quint32>(result);
	}

	for (UINT i = 0; i != count; i++) {
		winrt::com_ptr<IMMDevice> device;
		LPWSTR id = nullptr;
		if (FAILED(collection->Item(i, device.put())) || FAILED(device->GetId(&id))) continue;

		auto qid = QString::fromWCharArray(id);
		CoTaskMemFree(id);
		this->updateEndpoint(qid);
	}

	for (auto flow: {eRender, eCapture}) {
		winrt::com_ptr<IMMDevice> device;
		LPWSTR id = nullptr;
		auto qflow = flow == eRender ? AudioFlow::Output : AudioFlow::Input;

		// E_NOTFOUND when there is no endpoint of this kind.
		if (SUCCEEDED(this->impl->enumerator->GetDefaultAudioEndpoint(flow, eConsole, device.put()))
		    && SUCCEEDED(device->GetId(&id)))
		{
			this->defaultIds.at(qflow) = QString::fromWCharArray(id);
			CoTaskMemFree(id);
		}
	}

	this->resolveDefaults();
}

AudioService* AudioService::instance() {
	static auto* instance = new AudioService(); // NOLINT
	return instance;
}

AudioEndpoint* AudioService::find(const QString& id) const {
	for (auto* endpoint: this->mEndpoints.valueList()) {
		if (endpoint->id() == id) return endpoint;
	}

	return nullptr;
}

void AudioService::updateEndpoint(const QString& id) {
	if (!this->impl->enumerator) return;

	winrt::com_ptr<IMMDevice> device;
	DWORD state = DEVICE_STATE_NOTPRESENT;
	auto wid = id.toStdWString();

	if (FAILED(this->impl->enumerator->GetDevice(wid.c_str(), device.put()))
	    || FAILED(device->GetState(&state)) || !(state & LISTED_STATES))
	{
		this->removeEndpoint(id);
		return;
	}

	auto* endpoint = this->find(id);

	if (!endpoint) {
		auto mmEndpoint = device.try_as<IMMEndpoint>();
		auto flow = eAll;
		if (!mmEndpoint || FAILED(mmEndpoint->GetDataFlow(&flow))) return;

		endpoint = new AudioEndpoint(
		    new AudioEndpointPrivate(this, device),
		    id,
		    flow == eRender ? AudioFlow::Output : AudioFlow::Input,
		    this
		);

		endpoint->setState(static_cast<AudioEndpointState::Enum>(state));
		this->mEndpoints.insertObject(endpoint);
		this->resolveDefaults();
	} else {
		endpoint->setState(static_cast<AudioEndpointState::Enum>(state));
	}
}

void AudioService::removeEndpoint(const QString& id) {
	auto* endpoint = this->find(id);
	if (!endpoint) return;

	this->mEndpoints.removeObject(endpoint);
	this->resolveDefaults();
	endpoint->deleteLater();
}

void AudioService::updateDefault(AudioFlow::Enum flow, const QString& id) {
	this->defaultIds.at(flow) = id;
	this->resolveDefaults();
}

void AudioService::updateProperties(const QString& id) {
	if (auto* endpoint = this->find(id)) endpoint->refreshProperties();
}

void AudioService::updateVolume(const QString& id, qreal volume, bool muted) {
	if (auto* endpoint = this->find(id)) endpoint->updateVolume(volume, muted);
}

void AudioService::resolveDefaults() {
	Qt::beginPropertyUpdateGroup();
	this->bDefaultOutput = this->find(this->defaultIds.at(AudioFlow::Output));
	this->bDefaultInput = this->find(this->defaultIds.at(AudioFlow::Input));
	Qt::endPropertyUpdateGroup();
}

void AudioService::setDefault(AudioEndpoint* endpoint, AudioFlow::Enum flow) {
	if (!endpoint) {
		qCWarning(logAudio) << "Cannot unset the default" << AudioFlow::toString(flow);
		return;
	}

	if (endpoint->flow() != flow) {
		qCWarning(logAudio) << "Cannot make" << endpoint << "the default" << AudioFlow::toString(flow);
		return;
	}

	winrt::com_ptr<IPolicyConfig> policy;
	auto result = CoCreateInstance(
	    CLSID_POLICY_CONFIG_CLIENT,
	    nullptr,
	    CLSCTX_ALL,
	    __uuidof(IPolicyConfig),
	    policy.put_void()
	);

	if (FAILED(result)) {
		qCWarning(logAudio) << "Cannot set the default endpoint on this version of Windows, error"
		                    << Qt::hex << static_cast<quint32>(result);
		return;
	}

	auto wid = endpoint->id().toStdWString();
	for (auto role: {eConsole, eMultimedia, eCommunications}) {
		result = policy->SetDefaultEndpoint(wid.c_str(), role);

		if (FAILED(result)) {
			qCWarning(logAudio) << "Could not make" << endpoint << "the default for role" << role
			                    << "error" << Qt::hex << static_cast<quint32>(result);
		}
	}
}

AudioQml::AudioQml(QObject* parent): QObject(parent) {
	auto* service = AudioService::instance();

	// clang-format off
	QObject::connect(service, &AudioService::defaultOutputChanged, this, &AudioQml::defaultOutputChanged);
	QObject::connect(service, &AudioService::defaultInputChanged, this, &AudioQml::defaultInputChanged);
	// clang-format on
}

} // namespace qs::windows
