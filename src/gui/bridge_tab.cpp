#include "bridge_tab.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "logger.h"
#include "resource.h"
#include "ui_utils.h"

namespace wb {

namespace {

// Default latency per WASAPI mode (ms). Used for the hint text and to reset
// the edit when the user leaves an invalid value (0/empty/out-of-range).
constexpr int kLatencyDefaultShared    = 10;
constexpr int kLatencyDefaultExclusive = 5;

// Digit-only filter for the latency edit. We drop ES_NUMBER -- its built-in
// "Unacceptable Character" balloon leaves a gray repaint artifact on our
// non-WS_CLIPCHILDREN panel when re-shown -- and reject non-digits ourselves
// with a quiet beep. No balloon is ever shown, so the artifact can't occur.
LRESULT CALLBACK LatencyEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                 UINT_PTR, DWORD_PTR) {
    if (msg == WM_CHAR) {
        wchar_t ch = static_cast<wchar_t>(wp);
        // Let editing control chars through (backspace etc., < space); reject
        // any visible non-digit.
        if (ch >= L' ' && (ch < L'0' || ch > L'9')) {
            MessageBeep(MB_OK);
            return 0;
        }
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, LatencyEditProc, 1);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// Panel WndProc. The panel is parented to the tab control (canonical
// Win32 property-sheet pattern), which means notifications from the
// child controls go to the panel instead of the main window. Forward
// WM_COMMAND and WM_NOTIFY up to the top-level window so the existing
// handlers there keep working. Everything else falls through to
// DefWindowProc, which honours the registered class background.
COLORREF StateDotColor(BridgeState s);

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root && root != hwnd) {
            return SendMessageW(root, msg, wp, lp);
        }
    }
    if (msg == WM_SIZE) {
        AppState* st = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (st) {
            LayoutBridgeTab(st, LOWORD(lp), HIWORD(lp));
        }
    }
    if (msg == WM_CTLCOLORSTATIC) {
        // Paint static labels and group-box titles on the panel's white
        // background. Without this they'd inherit the system 3D-face colour
        // and look like grey patches sitting on a white sheet.
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    if (msg == WM_DRAWITEM) {
        // Owner-draw the state dot as a real filled circle. Sized and
        // centred to the rect handed to us, but with the visual circle
        // diameter clamped so it matches text cap-height alongside the
        // adjacent "Stopped"/"Running" label.
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        AppState* st = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (st && dis && dis->hwndItem == st->hLblStateDot) {
            // Background fill so we don't streak from a previous paint.
            FillRect(dis->hDC, &dis->rcItem, GetSysColorBrush(COLOR_WINDOW));

            BridgeState s = st->bridge ? st->bridge->State() : BridgeState::Stopped;
            COLORREF col = StateDotColor(s);

            const int rectW = dis->rcItem.right - dis->rcItem.left;
            const int rectH = dis->rcItem.bottom - dis->rcItem.top;
            int diameter = (rectH < rectW ? rectH : rectW) - 4;
            if (diameter > 12) diameter = 12;
            if (diameter < 6)  diameter = 6;
            int cx = (dis->rcItem.left + dis->rcItem.right) / 2;
            int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
            RECT circle{
                cx - diameter / 2,
                cy - diameter / 2,
                cx - diameter / 2 + diameter,
                cy - diameter / 2 + diameter,
            };

            // Filled rounded square: pixel-perfect at any size with no
            // antialiasing required. Sharper than a small Ellipse() and
            // friendlier to look at than a hard-cornered square.
            HBRUSH br  = CreateSolidBrush(col);
            HPEN   pen = CreatePen(PS_SOLID, 1, col);
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(dis->hDC, br));
            HPEN   oldPn = static_cast<HPEN>(SelectObject(dis->hDC, pen));
            const int radius = 4;
            RoundRect(dis->hDC,
                      circle.left, circle.top,
                      circle.right, circle.bottom,
                      radius, radius);
            SelectObject(dis->hDC, oldBr);
            SelectObject(dis->hDC, oldPn);
            DeleteObject(br);
            DeleteObject(pen);
            return TRUE;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND MakeStatic(HWND parent, int x, int y, int cx, int cy, int id, const wchar_t* text,
                DWORD extra = 0) {
    return CreateWindowExW(0, L"STATIC", text,
                           WS_CHILD | WS_VISIBLE | extra,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND MakeGroup(HWND parent, int x, int y, int cx, int cy, int id, const wchar_t* text,
               DWORD extra = 0) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX | extra,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND MakeButton(HWND parent, int x, int y, int cx, int cy, int id, const wchar_t* text,
                DWORD extra = 0) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | extra,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND MakeRadio(HWND parent, int x, int y, int cx, int cy, int id, const wchar_t* text,
               bool first) {
    // Standard Win32 radio-group convention:
    //   * The FIRST radio gets WS_GROUP and WS_TABSTOP — Tab lands on the
    //     group as a whole, focus parks on whichever radio is checked.
    //   * Subsequent radios get neither flag — arrow keys move within the
    //     group, Tab skips past it.
    // Giving every radio WS_TABSTOP makes Tab visit each one individually and
    // (via BS_AUTORADIOBUTTON) auto-select it on focus, scrambling the
    // latency hint.
    DWORD style = WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON;
    if (first) style |= WS_GROUP | WS_TABSTOP;
    return CreateWindowExW(0, L"BUTTON", text, style,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND MakeCheck(HWND parent, int x, int y, int cx, int cy, int id, const wchar_t* text,
               DWORD extra = 0) {
    return CreateWindowExW(0, L"BUTTON", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | extra,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND MakeEdit(HWND parent, int x, int y, int cx, int cy, int id, DWORD extra = 0) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

HWND MakeCombo(HWND parent, int x, int y, int cx, int cy, int id) {
    // Critical for the "invisible until hover" bug:
    //  - Use CBS_DROPDOWNLIST (no edit child),
    //  - Total height = visible row + dropdown room (cy includes drop-down).
    //  - Parent is the panel, not the main window, so z-order issues with
    //    the tab control's own children disappear.
    return CreateWindowExW(0, L"COMBOBOX", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                               CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                           x, y, cx, cy, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           nullptr, nullptr);
}

} // namespace

void LayoutBridgeTab(AppState* st, int panelW, int panelH);

HWND CreateBridgeTab(AppState* st, HWND hParent) {
    // Register the panel class with our forwarding WndProc and the standard
    // dialog-style background brush so the panel paints the same light-grey
    // surface as a plain dialog body. hParent is the tab control HWND.
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style  = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PanelProc;
        wc.hInstance   = st->hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"WBBridgePanel";
        RegisterClassExW(&wc);
        registered = true;
    }

    // No WS_CLIPCHILDREN: with group boxes (which only paint their border +
    // title), the panel must be allowed to fill the group box interiors with
    // the dialog background; otherwise stale pixels from hidden sibling panels
    // can show through. WS_CLIPSIBLINGS is still needed for sibling controls.
    HWND panel = CreateWindowExW(WS_EX_CONTROLPARENT,
                                 L"WBBridgePanel", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS,
                                 0, 0, 100, 100,
                                 hParent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BRIDGE_PANEL)),
                                 st->hInstance, nullptr);
    // Stash AppState* so PanelProc can route WM_SIZE -> LayoutBridgeTab.
    SetWindowLongPtrW(panel, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

    // Just create the controls at placeholder positions; LayoutBridgeTab does
    // the real work on the first WM_SIZE. Saves us writing the math twice.
    //
    // WS_GROUP marks a group boundary for the dialog manager's arrow-key
    // navigation (GetNextDlgGroupItem). Each cluster below starts at a
    // WS_GROUP control and runs until the next WS_GROUP, so Up/Down/Left/
    // Right wrap *within* the cluster instead of spilling out of the panel:
    //   * hGrpDevices  -> [Source, Target]
    //   * hBtnRescan   -> [Rescan]               (lone group: arrows do nothing)
    //   * hGrpMode     -> [WASAPI mode box]
    //   * hRadShared   -> [Shared, Exclusive]    (set in MakeRadio)
    //   * hGrpLatency  -> [latency edit]
    //   * hChkTray     -> [Minimize-to-tray, Start/Stop]
    st->hGrpDevices = MakeGroup(panel, 0, 0, 10, 10, IDC_GRP_DEVICES, L"Devices", WS_GROUP);
    MakeStatic(panel, 0, 0, 10, 10, IDC_LBL_SOURCE, L"Source");
    st->hCmbSource = MakeCombo(panel, 0, 0, 10, 200, IDC_CMB_SOURCE);
    MakeStatic(panel, 0, 0, 10, 10, IDC_LBL_TARGET, L"Target");
    st->hCmbTarget = MakeCombo(panel, 0, 0, 10, 200, IDC_CMB_TARGET);
    // WS_GROUP makes Rescan its own one-control group, so arrow keys on it
    // have nowhere to go and do nothing (instead of jumping to the combos).
    st->hBtnRescan = MakeButton(panel, 0, 0, 10, 10, IDC_BTN_RESCAN, L"Rescan", WS_GROUP);

    // WS_GROUP terminates Rescan's lone group and starts the mode cluster.
    st->hGrpMode = MakeGroup(panel, 0, 0, 10, 10, IDC_GRP_MODE, L"WASAPI mode", WS_GROUP);
    st->hRadShared    = MakeRadio(panel, 0, 0, 10, 10, IDC_RAD_SHARED,    L"Shared",    true);
    st->hRadExclusive = MakeRadio(panel, 0, 0, 10, 10, IDC_RAD_EXCLUSIVE, L"Exclusive", false);

    st->hGrpLatency = MakeGroup(panel, 0, 0, 10, 10, IDC_GRP_LATENCY, L"Latency", WS_GROUP);
    st->hEdtLatency = MakeEdit(panel, 0, 0, 10, 10, IDC_EDT_LATENCY, ES_RIGHT);
    // Digit-only input via subclass instead of ES_NUMBER (see LatencyEditProc).
    SetWindowSubclass(st->hEdtLatency, LatencyEditProc, 1, 0);
    MakeStatic(panel, 0, 0, 10, 10, IDC_LBL_LATENCY_UNIT, L"ms");
    // SS_CENTERIMAGE so the hint text is vertically centred inside its rect,
    // matching how the radio buttons render their labels. Without it, plain
    // STATIC text sits at the rect top and looks 1-2 px higher than the
    // radio label even when the rects are aligned.
    st->hLblHint = MakeStatic(panel, 0, 0, 10, 10, IDC_LBL_LATENCY_HINT, L"",
                              SS_CENTERIMAGE);

    // Bottom row: minimize-to-tray checkbox on the left, state dot+text in
    // the centre, Start/Stop button on the right. The coloured dot + word
    // ("Stopped"/"Running") is enough; no separate "State" caption needed.

    // Dot is owner-drawn (WM_DRAWITEM) so we can render a real filled circle
    // that sits at the optical centre of the row, independent of font
    // metrics. SS_OWNERDRAW asks the parent to do the painting.
    st->hLblStateDot = MakeStatic(panel, 0, 0, 10, 10, IDC_LBL_STATE_DOT,
                                  L"", SS_OWNERDRAW);
    st->hLblState = MakeStatic(panel, 0, 0, 10, 10, IDC_LBL_STATE, L"Stopped",
                               SS_CENTERIMAGE);

    st->hChkTray = MakeCheck(panel, 0, 0, 10, 10, IDC_CHK_MIN_TRAY, L"Minimize to tray", WS_GROUP);
    SendMessageW(st->hChkTray, BM_SETCHECK, BST_CHECKED, 0);
    // Not BS_DEFPUSHBUTTON: that would make the button a permanent default
    // (always blue when enabled). The message loop promotes whichever push
    // button has focus to the default look and demotes it when focus leaves,
    // so the blue outline follows focus like a real dialog.
    st->hBtnToggle = MakeButton(panel, 0, 0, 10, 10, IDC_BTN_TOGGLE, L"Start Bridge");

    ApplyUiFontRecursive(panel);

    // Defaults
    SendMessageW(st->hRadShared, BM_SETCHECK, BST_CHECKED, 0);
    WriteLatencyEdit(st);
    RefreshBridgeTabState(st);

    return panel;
}

void PopulateDeviceCombos(AppState* st);

void RescanDevices(AppState* st) {
    // Per user request: Rescan always wipes the current selection. Forces
    // the user to re-pick consciously after a hardware change.
    //
    // Cancel any in-flight themed hover crossfade first. A themed combobox
    // animates its hot (blue, mouse-over) state fading back to normal over
    // ~50-200ms via a uxtheme buffered-paint animation. If Rescan is clicked
    // while that fade is still running, CB_RESETCONTENT's repaint gets
    // blended into the animation buffer and the text appears to dissolve to
    // empty instead of clearing instantly. Stopping the animation makes the
    // reset paint cleanly. (The normal hover fade is untouched.)
    BufferedPaintStopAllAnimations(st->hCmbSource);
    BufferedPaintStopAllAnimations(st->hCmbTarget);

    st->selectedSourceDevice = {};
    st->selectedTargetDevice = {};
    st->devices = EnumeratePlaybackDevices();
    PopulateDeviceCombos(st);

    WB_LOG_INFO("Found %zu playback device(s).", st->devices.size());
    RefreshBridgeTabState(st);
}

void PopulateDeviceCombos(AppState* st) {
    SendMessageW(st->hCmbSource, CB_RESETCONTENT, 0, 0);
    SendMessageW(st->hCmbTarget, CB_RESETCONTENT, 0, 0);

    for (size_t i = 0; i < st->devices.size(); ++i) {
        std::wstring wname = Utf8ToWide(st->devices[i].name);
        // Store an index (plus one), never a pointer into `devices`: the
        // vector is replaced on every enumeration.
        LPARAM itemData = static_cast<LPARAM>(i + 1);
        LRESULT idx = SendMessageW(st->hCmbSource, CB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(wname.c_str()));
        if (idx != CB_ERR) {
            SendMessageW(st->hCmbSource, CB_SETITEMDATA,
                         static_cast<WPARAM>(idx), itemData);
        }
        idx = SendMessageW(st->hCmbTarget, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(wname.c_str()));
        if (idx != CB_ERR) {
            SendMessageW(st->hCmbTarget, CB_SETITEMDATA,
                         static_cast<WPARAM>(idx), itemData);
        }
    }
}

namespace {

int FindDeviceSelection(const std::vector<DeviceEntry>& devices,
                        const std::wstring& wanted) {
    if (wanted.empty()) return CB_ERR;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (std::wstring(devices[i].id.wasapi) == wanted) {
            return static_cast<int>(i);
        }
    }
    return CB_ERR;
}

int ComboDeviceIndex(AppState* st, HWND combo) {
    int selection = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selection < 0) return CB_ERR;
    LRESULT data = SendMessageW(combo, CB_GETITEMDATA, selection, 0);
    if (data <= 0 || static_cast<size_t>(data - 1) >= st->devices.size()) return CB_ERR;
    return static_cast<int>(data - 1);
}

void CaptureSelectedDevice(AppState* st, HWND combo,
                          AppState::DeviceSelection& snapshot) {
    int index = ComboDeviceIndex(st, combo);
    if (index == CB_ERR) return; // A placeholder keeps its existing snapshot.
    snapshot.id = std::wstring(st->devices[static_cast<size_t>(index)].id.wasapi);
    snapshot.displayName = Utf8ToWide(st->devices[static_cast<size_t>(index)].name);
}

std::wstring SelectedDeviceId(AppState* st, HWND combo,
                              const std::wstring& retained) {
    int index = ComboDeviceIndex(st, combo);
    if (index != CB_ERR) {
        return std::wstring(st->devices[static_cast<size_t>(index)].id.wasapi);
    }
    return retained;
}

} // namespace

void AutoRescanDevices(AppState* st) {
    // Capture both selections before replacing the device vector. The
    // snapshots are GUI-owned and remain valid while a device is unavailable.
    CaptureSelectedDevice(st, st->hCmbSource, st->selectedSourceDevice);
    CaptureSelectedDevice(st, st->hCmbTarget, st->selectedTargetDevice);

    BufferedPaintStopAllAnimations(st->hCmbSource);
    BufferedPaintStopAllAnimations(st->hCmbTarget);
    st->devices = EnumeratePlaybackDevices();
    PopulateDeviceCombos(st);

    auto addUnavailable = [st](HWND combo,
                               const AppState::DeviceSelection& snapshot) {
        if (snapshot.id.empty() || FindDeviceSelection(st->devices, snapshot.id) != CB_ERR) {
            return CB_ERR;
        }
        std::wstring label = snapshot.displayName + L" (unavailable)";
        LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(label.c_str()));
        if (index != CB_ERR) {
            // Zero is reserved for an unavailable placeholder; live entries
            // use their device index plus one.
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(index), 0);
        }
        return static_cast<int>(index);
    };

    // A missing persisted ID has no current-run snapshot, so keep its combo
    // empty until the exact device returns. A snapshot, on the other hand,
    // means the user selected that device during this run and must retain the
    // existing unavailable-placeholder behavior.
    const std::wstring sourceId = st->selectedSourceDevice.id.empty()
                                      ? st->persistedSourceDeviceId
                                      : st->selectedSourceDevice.id;
    const std::wstring targetId = st->selectedTargetDevice.id.empty()
                                      ? st->persistedTargetDeviceId
                                      : st->selectedTargetDevice.id;
    const int sourceDevice = FindDeviceSelection(st->devices, sourceId);
    const int targetDevice = FindDeviceSelection(st->devices, targetId);
    int source = sourceDevice;
    int target = targetDevice;
    if (source == CB_ERR && !st->selectedSourceDevice.id.empty()) {
        source = addUnavailable(st->hCmbSource, st->selectedSourceDevice);
    }
    if (target == CB_ERR && !st->selectedTargetDevice.id.empty()) {
        target = addUnavailable(st->hCmbTarget, st->selectedTargetDevice);
    }
    SendMessageW(st->hCmbSource, CB_SETCURSEL, source, 0);
    SendMessageW(st->hCmbTarget, CB_SETCURSEL, target, 0);

    if (sourceDevice != CB_ERR) {
        st->selectedSourceDevice.id = sourceId;
        st->selectedSourceDevice.displayName =
            Utf8ToWide(st->devices[static_cast<size_t>(sourceDevice)].name);
    }
    if (targetDevice != CB_ERR) {
        st->selectedTargetDevice.id = targetId;
        st->selectedTargetDevice.displayName =
            Utf8ToWide(st->devices[static_cast<size_t>(targetDevice)].name);
    }

    if (!st->selectedSourceDevice.id.empty() && source == CB_ERR) {
        WB_LOG_WARN("Auto-rescan: source device unavailable; placeholder retained.");
    }
    if (!st->selectedTargetDevice.id.empty() && target == CB_ERR) {
        WB_LOG_WARN("Auto-rescan: target device unavailable; placeholder retained.");
    }
    WB_LOG_INFO("Auto-rescan found %zu playback device(s).", st->devices.size());
    RefreshBridgeTabState(st);
}

void RestoreSettings(AppState* st, const Settings& settings) {
    st->persistedSourceDeviceId = settings.sourceDeviceId;
    st->persistedTargetDeviceId = settings.targetDeviceId;
    st->modeIsExclusive = settings.exclusiveMode;
    st->latencyShared = settings.sharedLatency;
    st->latencyExclusive = settings.exclusiveLatency;
    st->minimizeToTray = settings.minimizeToTray;

    int source = FindDeviceSelection(st->devices, settings.sourceDeviceId);
    int target = FindDeviceSelection(st->devices, settings.targetDeviceId);
    if (source != CB_ERR) {
        st->selectedSourceDevice.id = settings.sourceDeviceId;
        st->selectedSourceDevice.displayName = Utf8ToWide(st->devices[static_cast<size_t>(source)].name);
    }
    if (target != CB_ERR) {
        st->selectedTargetDevice.id = settings.targetDeviceId;
        st->selectedTargetDevice.displayName = Utf8ToWide(st->devices[static_cast<size_t>(target)].name);
    }
    SendMessageW(st->hCmbSource, CB_SETCURSEL, source, 0);
    SendMessageW(st->hCmbTarget, CB_SETCURSEL, target, 0);
    SendMessageW(st->hRadShared, BM_SETCHECK,
                 settings.exclusiveMode ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(st->hRadExclusive, BM_SETCHECK,
                 settings.exclusiveMode ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(st->hChkTray, BM_SETCHECK,
                 settings.minimizeToTray ? BST_CHECKED : BST_UNCHECKED, 0);
    WriteLatencyEdit(st);
    RefreshBridgeTabState(st);
}

void SaveCurrentSettings(AppState* st) {
    int currentLatency = ReadLatencyEdit(st);
    if (st->modeIsExclusive) st->latencyExclusive = currentLatency;
    else                     st->latencyShared = currentLatency;

    Settings settings;
    settings.sourceDeviceId = SelectedDeviceId(st, st->hCmbSource,
                                               st->persistedSourceDeviceId);
    settings.targetDeviceId = SelectedDeviceId(st, st->hCmbTarget,
                                               st->persistedTargetDeviceId);
    settings.exclusiveMode = st->modeIsExclusive;
    settings.sharedLatency = st->latencyShared;
    settings.exclusiveLatency = st->latencyExclusive;
    settings.minimizeToTray = st->minimizeToTray;
    SaveSettings(settings);
    st->persistedSourceDeviceId = settings.sourceDeviceId;
    st->persistedTargetDeviceId = settings.targetDeviceId;
}

int ReadLatencyEdit(AppState* st) {
    // Single sanitize rule used everywhere:
    //   empty / 0 / < 1  -> mode default (the user cleared it or typed 0)
    //   > 500            -> clamp to the 500 ms maximum
    //   1..500           -> kept as parsed (so "00100" reads as 100)
    wchar_t buf[32];
    GetWindowTextW(st->hEdtLatency, buf, 32);
    int v = _wtoi(buf);
    if (v < 1) {
        v = st->modeIsExclusive ? kLatencyDefaultExclusive : kLatencyDefaultShared;
    } else if (v > 500) {
        v = 500;
    }
    return v;
}

void WriteLatencyEdit(AppState* st) {
    // Drive everything from the authoritative tracked mode (set in
    // HandleBridgeCommand) instead of querying BM_GETCHECK, which can be
    // out of date during keyboard radio switches.
    bool excl = st->modeIsExclusive;
    int  val  = excl ? st->latencyExclusive : st->latencyShared;
    wchar_t buf[16];
    _snwprintf_s(buf, _TRUNCATE, L"%d", val);
    SetWindowTextW(st->hEdtLatency, buf);

    wchar_t hint[64];
    _snwprintf_s(hint, _TRUNCATE,
                 L"Default %d ms (%s)",
                 excl ? kLatencyDefaultExclusive : kLatencyDefaultShared,
                 excl ? L"Exclusive" : L"Shared");
    SetWindowTextW(st->hLblHint, hint);
}

// Sanitize the current edit text, store the result into the active mode's
// slot, then rewrite the edit with the canonical value. This is the single
// "commit" point: it strips leading zeros, applies the default for empty/0,
// and clamps >500 to 500. Called when the field loses focus, on mode switch,
// and before Start -- never per keystroke, so live typing isn't fought.
void CommitLatency(AppState* st) {
    int v = ReadLatencyEdit(st);
    if (st->modeIsExclusive) st->latencyExclusive = v;
    else                     st->latencyShared    = v;
    WriteLatencyEdit(st);
}

namespace {
const wchar_t* StateText(BridgeState s) {
    switch (s) {
        case BridgeState::Stopped:    return L"Stopped";
        case BridgeState::Starting:   return L"Starting";
        case BridgeState::Running:    return L"Running";
        case BridgeState::Recovering: return L"Recovering";
        case BridgeState::Stopping:   return L"Stopping";
        case BridgeState::Failed:     return L"Failed";
    }
    return L"Unknown";
}
COLORREF StateDotColor(BridgeState s) {
    // WireGuard-style palette: green when active, amber for transitions,
    // red for stopped/failed.
    switch (s) {
        case BridgeState::Running:    return RGB(0x34, 0xA8, 0x53);  // green
        case BridgeState::Starting:
        case BridgeState::Stopping:
        case BridgeState::Recovering: return RGB(0xF4, 0xB4, 0x00);  // amber
        case BridgeState::Failed:     return RGB(0xD9, 0x30, 0x25);  // red
        case BridgeState::Stopped:
        default:                      return RGB(0x80, 0x80, 0x80);  // grey
    }
}
} // namespace

void RefreshBridgeTabState(AppState* st) {
    BridgeState s = st->bridge ? st->bridge->State() : BridgeState::Stopped;
    bool running = (s != BridgeState::Stopped && s != BridgeState::Failed);

    // Keep the state-poll timer's change-detection baseline (lastSeenState)
    // in sync with whatever we paint here. ToggleBridge calls this directly,
    // and without updating lastSeenState the timer's "only refresh on change"
    // logic would skip later transitions (e.g. a transient Starting that
    // immediately fails back to Failed), leaving the label stuck on the
    // transient while the dot/tooltip read the real state.
    st->lastSeenState = s;

    // Caption shows "Stop Bridge" once the bridge is up or tearing down
    // (Running/Recovering/Stopping), and "Start Bridge" otherwise (Stopped/
    // Starting/Failed) -- so it never flashes "Start Bridge" mid-stop. The
    // separate `running` flag below still locks inputs the instant Starting
    // begins.
    bool active = (s == BridgeState::Running ||
                   s == BridgeState::Recovering ||
                   s == BridgeState::Stopping);
    SetWindowTextW(st->hLblState, StateText(s));
    SetWindowTextW(st->hBtnToggle, active ? L"Stop Bridge" : L"Start Bridge");
    InvalidateRect(st->hLblStateDot, nullptr, TRUE);

    // Lock inputs while the bridge is running.
    EnableWindow(st->hCmbSource,    !running);
    EnableWindow(st->hCmbTarget,    !running);
    EnableWindow(st->hBtnRescan,    !running);
    EnableWindow(st->hRadShared,    !running);
    EnableWindow(st->hRadExclusive, !running);
    EnableWindow(st->hEdtLatency,   !running);

    // Toggle button gating. Only enable it when there's a definite action:
    //  - Stop is meaningful only when Running/Recovering.
    //  - Start is meaningful only when Stopped/Failed (and selections valid).
    // The transient Starting/Stopping states leave the button disabled so the
    // user cannot stop a worker that's mid-init or start again while teardown
    // is still in progress.
    int srcSel = ComboDeviceIndex(st, st->hCmbSource);
    int tgtSel = ComboDeviceIndex(st, st->hCmbTarget);
    bool selectionsOk = (srcSel != CB_ERR && tgtSel != CB_ERR &&
                         srcSel != tgtSel);
    bool canStop  = (s == BridgeState::Running || s == BridgeState::Recovering);
    bool canStart = selectionsOk && (s == BridgeState::Stopped || s == BridgeState::Failed);
    EnableWindow(st->hBtnToggle, canStop || canStart);

    // Focus management around the Start/Stop transition. Guarded by
    // GetActiveWindow()==hMain so the periodic state timer never steals focus
    // while the app is in the background.
    if (GetActiveWindow() == st->hMain) {
        HWND f = GetFocus();
        if (f == nullptr || !IsWindowEnabled(f)) {
            // Focus was dropped because we just disabled the control that had
            // it (the Start button, on click -> Starting). Park it on the tab
            // strip so keyboard nav stays alive, then hide the focus ring so
            // it looks exactly like a freshly-opened window: focus present,
            // no ring drawn until the user presses Tab/arrow (Win32 "keyboard
            // cues", UISF_HIDEFOCUS).
            if (st->hTab) SetFocus(st->hTab);
            SendMessageW(st->hMain, WM_CHANGEUISTATE,
                         MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
        } else if (st->wantRefocusToggle && IsWindowEnabled(st->hBtnToggle)) {
            // Bridge reached Running and the Stop button is live again. If the
            // focus ring is still hidden (UISF_HIDEFOCUS set) AND focus is
            // still parked on the tab strip, the user hasn't pressed a nav key
            // since Start, so quietly move focus onto Stop -- Space/Enter then
            // stops the bridge, with no visible ring. If the ring is showing
            // or focus moved, the user navigated during the transient; leave
            // their focus exactly where it is.
            LRESULT ui = SendMessageW(st->hMain, WM_QUERYUISTATE, 0, 0);
            if ((ui & UISF_HIDEFOCUS) && f == st->hTab) {
                SetFocus(st->hBtnToggle);
            }
            st->wantRefocusToggle = false;
        }
    }
}

bool HandleBridgeCommand(AppState* st, WORD ctrlId, WORD notifyCode) {
    switch (ctrlId) {
        case IDC_BTN_RESCAN:
            if (notifyCode == BN_CLICKED) {
                RescanDevices(st);
                return true;
            }
            break;
        case IDC_CMB_SOURCE:
        case IDC_CMB_TARGET:
            if (notifyCode == CBN_SELCHANGE) {
                HWND combo = ctrlId == IDC_CMB_SOURCE ? st->hCmbSource : st->hCmbTarget;
                int index = ComboDeviceIndex(st, combo);
                if (index != CB_ERR) {
                    auto& snapshot = ctrlId == IDC_CMB_SOURCE
                                          ? st->selectedSourceDevice
                                          : st->selectedTargetDevice;
                    snapshot.id = std::wstring(st->devices[static_cast<size_t>(index)].id.wasapi);
                    snapshot.displayName = Utf8ToWide(st->devices[static_cast<size_t>(index)].name);
                    if (ctrlId == IDC_CMB_SOURCE) st->persistedSourceDeviceId = snapshot.id;
                    else st->persistedTargetDeviceId = snapshot.id;
                }
                RefreshBridgeTabState(st);
                SaveCurrentSettings(st);
                return true;
            }
            break;
        case IDC_RAD_SHARED:
        case IDC_RAD_EXCLUSIVE:
            if (notifyCode == BN_CLICKED) {
                // BS_AUTORADIOBUTTON fires BN_CLICKED even when the user
                // clicks the already-active radio. Compare to the tracked
                // mode so we don't copy the current edit value into the
                // *other* slot when nothing actually changed.
                bool nowExcl = (ctrlId == IDC_RAD_EXCLUSIVE);
                if (nowExcl == st->modeIsExclusive) return true;
                // Mode changed: save current edit into the OLD slot, then
                // load the value remembered for the NEW slot.
                int prev = ReadLatencyEdit(st);
                if (st->modeIsExclusive) st->latencyExclusive = prev;
                else                     st->latencyShared    = prev;
                st->modeIsExclusive = nowExcl;
                WriteLatencyEdit(st);
                SaveCurrentSettings(st);
                return true;
            }
            break;
        case IDC_BTN_TOGGLE:
            if (notifyCode == BN_CLICKED) {
                ToggleBridge(st);
                return true;
            }
            break;
        case IDC_CHK_MIN_TRAY:
            if (notifyCode == BN_CLICKED) {
                st->minimizeToTray = (SendMessageW(st->hChkTray, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SaveCurrentSettings(st);
                return true;
            }
            break;
        case IDC_EDT_LATENCY:
            if (notifyCode == EN_KILLFOCUS) {
                CommitLatency(st);
                return true;
            }
            break;
        // Do not normalize the latency edit per keystroke. Commit when focus
        // loss so the value is persisted without fighting live typing. The
        // mode-switch and Start paths also commit before using the value.
    }
    return false;
}

void ToggleBridge(AppState* st) {
    if (!st->bridge) return;

    // Decide stop-vs-start from the displayed State(), not IsRunning(). The
    // button caption is derived from State() in RefreshBridgeTabState, so
    // using the same source here guarantees the button does what it says.
    // (Using IsRunning() drifted: after a failed start the caption could read
    // "Stop Bridge" while running_ was false, so a click ran the Start path.)
    // Stop() is safe to call even when the worker already self-exited.
    BridgeState s = st->bridge->State();
    bool active = (s != BridgeState::Stopped && s != BridgeState::Failed);
    if (active) {
        WB_LOG_INFO("User requested bridge stop.");
        // Non-blocking: signal the worker and return. Blocking Stop() here
        // would join() on the GUI thread and freeze the window for the whole
        // WASAPI teardown (long at high latency), hiding the Stopping state
        // and queuing user clicks. RequestStop lets the worker tear down on
        // its own; the 250ms state timer paints Stopping then Stopped.
        st->bridge->RequestStop();
        // Arm the deferred focus move so that once the worker reaches Stopped
        // and the toggle button re-enables (now as "Start Bridge"), focus
        // lands invisibly on it -- mirroring the Start->Stop refocus.
        st->wantRefocusToggle = true;
        RefreshBridgeTabState(st);
        return;
    }

    int srcSel = ComboDeviceIndex(st, st->hCmbSource);
    int tgtSel = ComboDeviceIndex(st, st->hCmbTarget);
    if (srcSel == CB_ERR || tgtSel == CB_ERR || srcSel == tgtSel) {
        MessageBoxW(st->hMain,
                    L"Please pick available, distinct Source and Target devices first.",
                    L"WASAPI Bridge", MB_OK | MB_ICONINFORMATION);
        return;
    }

    BridgeConfig cfg{};
    cfg.sourceDeviceId = st->devices[static_cast<size_t>(srcSel)].id;
    cfg.targetDeviceId = st->devices[static_cast<size_t>(tgtSel)].id;
    bool excl = st->modeIsExclusive;  // authoritative; see WriteLatencyEdit
    cfg.shareMode = excl ? ma_share_mode_exclusive : ma_share_mode_shared;

    // Validate target's advertised mode the same way the CLI did.
    const auto& tgt = st->devices[static_cast<size_t>(tgtSel)];
    if (excl && !tgt.supportsExclusive) {
        MessageBoxW(st->hMain,
                    L"Target device doesn't advertise exclusive mode.\n"
                    L"Pick a different target or switch to Shared.",
                    L"WASAPI Bridge", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!excl && !tgt.supportsShared) {
        MessageBoxW(st->hMain,
                    L"Target device doesn't advertise shared mode.",
                    L"WASAPI Bridge", MB_OK | MB_ICONWARNING);
        return;
    }

    // Normalize the latency before starting so the edit shows exactly what
    // the bridge will use (default for empty/0, clamp >500, strip leading
    // zeros). Covers the case where Start is pressed without the field ever
    // losing focus (e.g. typed then clicked, or activated by keyboard).
    CommitLatency(st);
    cfg.targetLatency = static_cast<ma_uint32>(ReadLatencyEdit(st));
    st->persistedSourceDeviceId = std::wstring(st->devices[static_cast<size_t>(srcSel)].id.wasapi);
    st->persistedTargetDeviceId = std::wstring(st->devices[static_cast<size_t>(tgtSel)].id.wasapi);
    SaveCurrentSettings(st);

    WB_LOG_INFO("Starting bridge: source=\"%s\" target=\"%s\" mode=%s latency=%u ms",
                st->devices[static_cast<size_t>(srcSel)].name.c_str(),
                tgt.name.c_str(),
                excl ? "Exclusive" : "Shared",
                cfg.targetLatency);

    if (!st->bridge->Start(cfg)) {
        MessageBoxW(st->hMain,
                    L"Failed to start the bridge. See the Log tab for details.",
                    L"WASAPI Bridge", MB_OK | MB_ICONERROR);
    } else {
        // Arm the deferred focus move: once the worker reaches Running and the
        // state timer re-enables the toggle button, RefreshBridgeTabState will
        // shift focus from the (invisibly focused) tab strip onto Stop -- but
        // only if the user hasn't pressed a nav key in the meantime.
        st->wantRefocusToggle = true;
    }
    RefreshBridgeTabState(st);
}

void LayoutBridgeTab(AppState* st, int panelW, int panelH) {
    if (panelW <= 0 || panelH <= 0) return;

    const int M     = 10;   // outer panel margin
    const int GP    = 10;   // group-box horizontal padding
    const int GTOP  = 22;   // group-box top padding (clears the title)
    const int GBOT  = 12;   // group-box bottom padding
    const int LH    = 22;   // label / radio height
    const int CH    = 24;   // combo / edit height
    const int BH    = 26;   // small button height (Rescan)
    const int LBLW  = 56;   // "Source" / "Target" label width
    const int BTNW  = 150;  // Start/Stop button width
    const int BTNH  = 30;
    const int BIGW  = panelW - 2 * M;
    int y = M;

    // ---- Devices group ----
    int devInner = CH + 8 + CH + 10 + BH;
    int devH     = GTOP + devInner + GBOT;
    SetWindowPos(st->hGrpDevices, nullptr, M, y, BIGW, devH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int gx     = M + GP;
    int comboX = gx + LBLW;
    int comboW = BIGW - 2 * GP - LBLW;
    int gy     = y + GTOP;

    SetWindowPos(GetDlgItem(GetParent(st->hCmbSource), IDC_LBL_SOURCE), nullptr,
                 gx, gy + 3, LBLW, LH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hCmbSource, nullptr,
                 comboX, gy, comboW, 200, SWP_NOZORDER | SWP_NOACTIVATE);
    gy += CH + 8;

    SetWindowPos(GetDlgItem(GetParent(st->hCmbTarget), IDC_LBL_TARGET), nullptr,
                 gx, gy + 3, LBLW, LH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hCmbTarget, nullptr,
                 comboX, gy, comboW, 200, SWP_NOZORDER | SWP_NOACTIVATE);
    gy += CH + 10;

    // Rescan: right-aligned on its own row inside the Devices group.
    SetWindowPos(st->hBtnRescan, nullptr,
                 M + BIGW - GP - 90, gy, 90, BH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // +7 (not +8): nudges the Mode/Latency row up 1px so the gap below it
    // (to the bottom-anchored Start button) gains 1px and the gap above it
    // (to the Devices box) loses 1px. Balances the visual spacing.
    y += devH + 7;

    // ---- Mode + Latency on one row ----
    int modeInner = LH + 6 + LH;
    int modeBoxH  = GTOP + modeInner + GBOT;
    int latInner  = CH + 6 + LH;
    int latBoxH   = GTOP + latInner + GBOT;
    int rowH      = (modeBoxH > latBoxH) ? modeBoxH : latBoxH;

    int modeW = 170;
    SetWindowPos(st->hGrpMode, nullptr, M, y, modeW, rowH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hRadShared, nullptr,
                 M + GP, y + GTOP, modeW - 2 * GP, LH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hRadExclusive, nullptr,
                 M + GP, y + GTOP + LH + 6, modeW - 2 * GP, LH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int latX = M + modeW + 8;
    int latW = BIGW - modeW - 8;
    SetWindowPos(st->hGrpLatency, nullptr, latX, y, latW, rowH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hEdtLatency, nullptr,
                 latX + GP, y + GTOP, 70, CH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(GetParent(st->hEdtLatency), IDC_LBL_LATENCY_UNIT),
                 nullptr, latX + GP + 78, y + GTOP + 3, 30, LH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    // Hint Y/height match the Exclusive radio so both texts sit on the same
    // baseline (combined with SS_CENTERIMAGE on the static).
    SetWindowPos(st->hLblHint, nullptr,
                 latX + GP, y + GTOP + LH + 6, latW - 2 * GP, LH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // ---- Bottom row: [Min-tray] [● State centred] [Start/Stop] ----
    // Anchored to the panel bottom so resizing taller adds airspace above.
    const int botRowH = BTNH;
    int byTop = panelH - M - botRowH;
    // Guard offset is +9 (not +8) to compensate for the row's -1px nudge
    // above: when this clamp is active it keeps the bottom block (tray,
    // state, Start) at its original Y, so only the Mode/Latency row moves
    // up. Net effect: Devices->row gap 11px AND row->Start gap 11px.
    if (byTop < y + rowH + 9) byTop = y + rowH + 9;

    // Tray checkbox: left.
    int chkY = byTop + (botRowH - LH) / 2;
    SetWindowPos(st->hChkTray, nullptr,
                 M, chkY, 180, LH, SWP_NOZORDER | SWP_NOACTIVATE);

    // Start/Stop button: right.
    SetWindowPos(st->hBtnToggle, nullptr,
                 M + BIGW - BTNW, byTop, BTNW, BTNH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Dot + state label centred between the two endpoints.
    const int dotSz = 18;
    SIZE sz{};
    HDC dc = GetDC(st->hLblState);
    HFONT oldFont = static_cast<HFONT>(SelectObject(dc, GetUiFont()));
    GetTextExtentPoint32W(dc, L"Recovering", 10, &sz); // worst-case width
    SelectObject(dc, oldFont);
    ReleaseDC(st->hLblState, dc);
    int textW = sz.cx + 8;
    int blockW = dotSz + 6 + textW;
    int leftEnd  = M + 180 + 12;
    int rightEnd = M + BIGW - BTNW - 12;
    int blockX   = (leftEnd + rightEnd - blockW) / 2;
    if (blockX < leftEnd) blockX = leftEnd;

    int dotY = byTop + (botRowH - dotSz) / 2;
    SetWindowPos(st->hLblStateDot, nullptr,
                 blockX, dotY, dotSz, dotSz,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    int textY = byTop + (botRowH - LH) / 2;
    SetWindowPos(st->hLblState, nullptr,
                 blockX + dotSz + 6, textY, textW, LH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    InvalidateRect(st->hGrpDevices, nullptr, TRUE);
    InvalidateRect(st->hGrpMode,    nullptr, TRUE);
    InvalidateRect(st->hGrpLatency, nullptr, TRUE);
}

} // namespace wb
