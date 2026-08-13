#include "settings.h"

#include <windows.h>

#include <cerrno>
#include <cwchar>
#include <vector>

#include "logger.h"

namespace wb {
namespace {

constexpr wchar_t kSection[] = L"Settings";
constexpr int kDefaultSharedLatency = 10;
constexpr int kDefaultExclusiveLatency = 5;
constexpr int kMaxLatency = 500;
constexpr size_t kMaxDeviceIdLength = 63;

std::wstring ConfigPath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                          static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            WB_LOG_WARN("Settings: GetModuleFileNameW failed (%lu).", GetLastError());
            return {};
        }
        if (length < buffer.size() - 1) {
            std::wstring path(buffer.data(), length);
            size_t slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos) path.resize(slash + 1);
            return path + L"config.ini";
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring ReadValue(const wchar_t* key, bool& present) {
    constexpr wchar_t kMissing[] = L"\x01";
    wchar_t buffer[256]{};
    DWORD length = GetPrivateProfileStringW(kSection, key, kMissing,
                                             buffer, static_cast<DWORD>(_countof(buffer)),
                                             ConfigPath().c_str());
    present = !(length == 1 && buffer[0] == kMissing[0]);
    return std::wstring(buffer, length);
}

bool ParseBool(const std::wstring& text, bool& value) {
    if (text == L"1" || text == L"true" || text == L"TRUE") {
        value = true;
        return true;
    }
    if (text == L"0" || text == L"false" || text == L"FALSE") {
        value = false;
        return true;
    }
    return false;
}

bool ParseLatency(const std::wstring& text, int& value) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    errno = 0;
    long parsed = std::wcstol(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() ||
        (*end != L' ' && *end != L'\t' && *end != L'\r' &&
         *end != L'\n' && *end != L'\0')) {
        return false;
    }
    while (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n') ++end;
    if (*end != L'\0' || parsed < 1 || parsed > kMaxLatency) return false;
    value = static_cast<int>(parsed);
    return true;
}

void WarnInvalid(const wchar_t* field) {
    char name[64]{};
    WideCharToMultiByte(CP_UTF8, 0, field, -1, name, static_cast<int>(sizeof(name)), nullptr, nullptr);
    WB_LOG_WARN("Settings: invalid %s; using a safe default.", name);
}

bool WriteValue(const wchar_t* key, const std::wstring& value, const std::wstring& path) {
    return WritePrivateProfileStringW(kSection, key, value.c_str(), path.c_str()) != FALSE;
}

} // namespace

Settings LoadSettings() {
    Settings settings;
    bool present = false;
    std::wstring value = ReadValue(L"source_device_id", present);
    if (present) settings.sourceDeviceId = value.substr(0, kMaxDeviceIdLength);
    if (value.size() > kMaxDeviceIdLength) WarnInvalid(L"source_device_id");

    value = ReadValue(L"target_device_id", present);
    if (present) settings.targetDeviceId = value.substr(0, kMaxDeviceIdLength);
    if (value.size() > kMaxDeviceIdLength) WarnInvalid(L"target_device_id");

    value = ReadValue(L"exclusive_mode", present);
    if (present && !ParseBool(value, settings.exclusiveMode)) {
        settings.exclusiveMode = false;
        WarnInvalid(L"exclusive_mode");
    }

    value = ReadValue(L"shared_latency", present);
    if (present && !ParseLatency(value, settings.sharedLatency)) {
        settings.sharedLatency = kDefaultSharedLatency;
        WarnInvalid(L"shared_latency");
    }

    value = ReadValue(L"exclusive_latency", present);
    if (present && !ParseLatency(value, settings.exclusiveLatency)) {
        settings.exclusiveLatency = kDefaultExclusiveLatency;
        WarnInvalid(L"exclusive_latency");
    }

    value = ReadValue(L"minimize_to_tray", present);
    if (present && !ParseBool(value, settings.minimizeToTray)) {
        settings.minimizeToTray = true;
        WarnInvalid(L"minimize_to_tray");
    }
    return settings;
}

bool SaveSettings(const Settings& settings) {
    std::wstring path = ConfigPath();
    if (path.empty()) {
        WB_LOG_WARN("Settings: failed to save config.ini; executable path is unavailable.");
        return false;
    }

    wchar_t shared[16]{};
    wchar_t exclusive[16]{};
    _snwprintf_s(shared, _TRUNCATE, L"%d", settings.sharedLatency);
    _snwprintf_s(exclusive, _TRUNCATE, L"%d", settings.exclusiveLatency);
    bool ok = WriteValue(L"source_device_id", settings.sourceDeviceId.substr(0, kMaxDeviceIdLength), path);
    ok = WriteValue(L"target_device_id", settings.targetDeviceId.substr(0, kMaxDeviceIdLength), path) && ok;
    ok = WriteValue(L"exclusive_mode", settings.exclusiveMode ? L"1" : L"0", path) && ok;
    ok = WriteValue(L"shared_latency", shared, path) && ok;
    ok = WriteValue(L"exclusive_latency", exclusive, path) && ok;
    ok = WriteValue(L"minimize_to_tray", settings.minimizeToTray ? L"1" : L"0", path) && ok;
    if (!ok) WB_LOG_WARN("Settings: failed to save config.ini (%lu).", GetLastError());
    return ok;
}

} // namespace wb
