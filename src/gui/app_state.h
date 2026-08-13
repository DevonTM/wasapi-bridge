#ifndef WB_GUI_APP_STATE_H
#define WB_GUI_APP_STATE_H

#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "bridge_service.h"
#include "device_enum.h"
#include "device_notifications.h"
#include "../types.h"
#include "miniaudio.h"

namespace wb {

// Custom messages that ferry events from background threads to the GUI
// thread. Keep them clustered here so we can see at a glance which IDs
// are taken.
enum : UINT {
    WM_APP_LOG_PUSHED        = WM_APP + 1,  // Logger -> log tab
    WM_APP_TRAYICON          = WM_APP + 2,  // Tray icon callback
    WM_APP_ACTIVATE_EXISTING = WM_APP + 3,  // Second launch -> main window
    WM_APP_DEVICE_CHANGED    = WM_APP + 4,  // Core Audio callback -> GUI
};

constexpr UINT_PTR kStateTimerId = 1;
constexpr UINT     kStateTimerMs = 250;

constexpr UINT_PTR kLogFlushTimerId = 2;
constexpr UINT     kLogFlushMs      = 80;

constexpr UINT_PTR kAutoRescanTimerId = 3;
constexpr UINT     kAutoRescanMs      = 300;

// Single source of truth for the GUI. The main window proc and helper
// modules read/write this through pointers stored in window user data.
struct AppState {
    HINSTANCE hInstance = nullptr;
    HWND      hMain     = nullptr;   // top-level window
    HWND      hTab      = nullptr;   // SysTabControl32
    HWND      panels[3] = {};        // bridge / log / about panels
    HICON     hIconLarge = nullptr;
    HICON     hIconSmall = nullptr;

    // Bridge tab handles
    HWND hCmbSource    = nullptr;
    HWND hCmbTarget    = nullptr;
    HWND hBtnRescan    = nullptr;
    HWND hRadShared    = nullptr;
    HWND hRadExclusive = nullptr;
    HWND hEdtLatency   = nullptr;
    HWND hLblHint      = nullptr;
    HWND hLblStateDot  = nullptr;  // colored "●"
    HWND hLblState     = nullptr;
    HWND hBtnToggle    = nullptr;
    HWND hChkTray      = nullptr;
    HWND hGrpDevices   = nullptr;
    HWND hGrpMode      = nullptr;
    HWND hGrpLatency   = nullptr;

    // Log tab handles
    HWND hEdtLog        = nullptr;
    HWND hChkAutoScroll = nullptr;
    HWND hBtnExport     = nullptr;
    HWND hBtnClear      = nullptr;

    // About tab handles
    HWND hStaticIcon       = nullptr;
    HWND hBtnResetSettings = nullptr;

    // App data
    struct DeviceSelection {
        std::wstring id;
        std::wstring displayName;
    };
    std::unique_ptr<BridgeService> bridge;
    std::vector<DeviceEntry>       devices;
    // Keep requested IDs even when a device is temporarily absent.
    std::wstring persistedSourceDeviceId;
    std::wstring persistedTargetDeviceId;
    // GUI-owned snapshots avoid pointers into `devices`, which is replaced on
    // every enumeration. Auto-rescan uses these to render unavailable entries.
    DeviceSelection selectedSourceDevice;
    DeviceSelection selectedTargetDevice;
    int latencyShared    = 10;
    int latencyExclusive = 5;
    bool modeIsExclusive = false;  // tracks which radio is currently active
    bool minimizeToTray  = true;
    // Set true by ToggleBridge after a successful Start(); consumed by
    // RefreshBridgeTabState to move focus onto the Stop button once the
    // bridge reaches Running (only if the user hasn't pressed Tab meanwhile).
    bool wantRefocusToggle = false;
    bool trayIconVisible = false;
    bool exitInProgress = false;
    bool logFlushPending = false;
    bool autoRescanPending = false;
    bool sourceComboDropdownOpen = false;
    bool targetComboDropdownOpen = false;
    DeviceNotificationMonitor deviceNotifications;
    BridgeState lastSeenState = BridgeState::Stopped;
};

inline AppState* GetAppState(HWND hwnd) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

} // namespace wb

#endif // WB_GUI_APP_STATE_H
