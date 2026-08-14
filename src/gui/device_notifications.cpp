#include "device_notifications.h"

#include <mmdeviceapi.h>

#include <atomic>

#include "logger.h"

namespace wb {
namespace {

// Keep the MMDevice enumerator CLSID local instead of relying on the SDK's
// external DEFINE_GUID symbol, which is not consistently provided by all
// MSVC Windows SDK/linker combinations.
constexpr CLSID kMmDeviceEnumeratorClsid{
    0xbcde0395, 0xe52f, 0x467c,
    {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}
};

} // namespace

class DeviceNotificationMonitor::Client final : public IMMNotificationClient {
public:
    void SetTarget(HWND window, UINT message) {
        window_.store(window, std::memory_order_release);
        message_.store(message, std::memory_order_release);
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG refs = --refs_;
        if (refs == 0) delete this;
        return refs;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        PostChange();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        PostChange();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        PostChange();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        PostChange();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        PostChange();
        return S_OK;
    }

private:
    void PostChange() const {
        HWND window = window_.load(std::memory_order_acquire);
        UINT message = message_.load(std::memory_order_acquire);
        if (window && message) PostMessageW(window, message, 0, 0);
    }

    std::atomic<ULONG> refs_{1};
    std::atomic<HWND> window_{nullptr};
    std::atomic<UINT> message_{0};
};

DeviceNotificationMonitor::~DeviceNotificationMonitor() {
    Stop();
}

bool DeviceNotificationMonitor::Start(HWND notifyWindow, UINT notifyMessage) {
    Stop();

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        WB_LOG_WARN("Audio device notifications unavailable: CoInitializeEx failed (0x%08lx).",
                    static_cast<unsigned long>(hr));
        return false;
    }
    comInitialized_ = true;

    IMMDeviceEnumerator* enumerator = nullptr;
    hr = CoCreateInstance(kMmDeviceEnumeratorClsid, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        WB_LOG_WARN("Audio device notifications unavailable: MMDeviceEnumerator creation failed (0x%08lx).",
                    static_cast<unsigned long>(hr));
        CoUninitialize();
        comInitialized_ = false;
        return false;
    }

    Client* client = new Client();
    client->SetTarget(notifyWindow, notifyMessage);
    hr = enumerator->RegisterEndpointNotificationCallback(client);
    if (FAILED(hr)) {
        WB_LOG_WARN("Audio device notifications unavailable: registration failed (0x%08lx).",
                    static_cast<unsigned long>(hr));
        client->Release();
        enumerator->Release();
        CoUninitialize();
        comInitialized_ = false;
        return false;
    }

    client_ = client;
    enumerator_ = enumerator;
    return true;
}

void DeviceNotificationMonitor::Stop() {
    Client* client = static_cast<Client*>(client_);
    IMMDeviceEnumerator* enumerator = static_cast<IMMDeviceEnumerator*>(enumerator_);
    if (!client && !enumerator) {
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
        return;
    }

    if (client) client->SetTarget(nullptr, 0);
    if (enumerator && client) {
        HRESULT hr = enumerator->UnregisterEndpointNotificationCallback(client);
        if (FAILED(hr)) {
            WB_LOG_WARN("Audio device notification cleanup failed (0x%08lx).",
                        static_cast<unsigned long>(hr));
        }
    }
    if (client) client->Release();
    if (enumerator) enumerator->Release();
    client_ = nullptr;
    enumerator_ = nullptr;

    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

} // namespace wb
