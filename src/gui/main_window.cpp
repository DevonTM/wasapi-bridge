#include "main_window.h"

#include <commctrl.h>
#include <shellapi.h>
#include <cstdio>

#include "../version.h"
#include "about_tab.h"
#include "app_state.h"
#include "bridge_service.h"
#include "bridge_tab.h"
#include "log_tab.h"
#include "logger.h"
#include "resource.h"
#include "tray.h"
#include "ui_utils.h"

namespace wb {

namespace {

constexpr const wchar_t* kWindowClass = L"WasapiBridgeMain";

// Forward declarations.
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
void OnCreate(HWND hwnd, AppState* st);
void OnSize(AppState* st);
void OnTabSelChange(AppState* st);
void OnStateTimer(AppState* st);
void OnLogPushed(AppState* st);
bool ConfirmExitWhileRunning(AppState* st);
void HandleClose(AppState* st);
void HandleSysCommand(AppState* st, WPARAM cmd);
void RestoreFromTray(AppState* st);
void TabAreaToRect(HWND hTab, RECT& outDisplay);

// --- main entry implemented at the bottom of the file ---

} // namespace

int RunGui(HINSTANCE hInstance, int nCmdShow) {
    // ICC_TAB_CLASSES, ICC_LINK_CLASS, ICC_STANDARD_CLASSES are in v6 manifest;
    // initialise once.
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    // Register the main window class.
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    wc.hIcon         = reinterpret_cast<HICON>(LoadImageW(hInstance,
                                                          MAKEINTRESOURCEW(IDI_APP_ICON),
                                                          IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR | LR_SHARED));
    wc.hIconSm       = reinterpret_cast<HICON>(LoadImageW(hInstance,
                                                          MAKEINTRESOURCEW(IDI_APP_ICON),
                                                          IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR | LR_SHARED));
    if (!RegisterClassExW(&wc)) {
        return -1;
    }

    AppState st;
    st.hInstance = hInstance;
    st.bridge    = std::make_unique<BridgeService>();
    st.hIconLarge = wc.hIcon;
    st.hIconSmall = wc.hIconSm;

    // Compute desired window size in DPI of the primary monitor.
    int cx = ScaleSystem(480);
    int cy = ScaleSystem(320);
    RECT desired{0, 0, cx, cy};
    // WS_OVERLAPPEDWINDOW already includes WS_THICKFRAME (resize frame) and
    // WS_MAXIMIZEBOX. We want both so the user can resize and maximise.
    AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int winW = desired.right - desired.left;
    int winH = desired.bottom - desired.top;
    int x = (screenW - winW) / 2;
    int y = (screenH - winH) / 2;

    // WS_EX_CONTROLPARENT on the top-level window so IsDialogMessageW has a
    // clear navigation root when wrapping focus past the last tabstop.
    HWND hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, kWindowClass, L"WASAPI Bridge",
                                WS_OVERLAPPEDWINDOW,
                                x, y, winW, winH,
                                nullptr, nullptr, hInstance, &st);
    if (!hwnd) {
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Tracks which push button currently wears the "default" (blue) look so
    // we can move it with focus and clear it when focus leaves all buttons.
    // DefWindowProc (our window/panels aren't real dialogs) doesn't do this
    // DM_SETDEFID bookkeeping, so without it the blue outline sticks on the
    // last button even after Tab moves focus elsewhere.
    HWND defBtn = nullptr;
    auto isPushButton = [](HWND h) -> bool {
        if (!h) return false;
        wchar_t cls[16];
        if (GetClassNameW(h, cls, 16) == 0) return false;
        if (lstrcmpiW(cls, L"Button") != 0) return false;
        LONG style = static_cast<LONG>(GetWindowLongPtrW(h, GWL_STYLE));
        LONG type  = style & 0x0F;  // BS_TYPEMASK low bits
        return type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON;
    };

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // Keyboard navigation spans two containers: the tab strip (a
        // WS_TABSTOP child of the main window) and the active panel (a
        // WS_EX_CONTROLPARENT child of the strip). IsDialogMessageW only
        // navigates within a single container, so:
        //   1. We bridge the strip<->panel boundary for Tab/Shift+Tab here.
        //   2. We route every other dialog message to whichever container
        //      currently owns the focus. Pumping IsDialogMessageW against the
        //      panel (not the main window) when focus is inside it keeps
        //      arrow/group navigation from leaking back onto the strip, which
        //      was the cause of Up/Left and Shift+Tab jumping to the tabs.
        HWND focus = GetFocus();
        int  sel   = TabCtrl_GetCurSel(st.hTab);
        HWND panel = (sel >= 0 && sel < 3) ? st.panels[sel] : nullptr;
        bool focusOnStrip = (focus == st.hTab);
        bool focusInPanel = (panel && IsChild(panel, focus));

        // Make the "default" (blue) button follow keyboard focus like a real
        // dialog. Promote the focused push button, demote the previous one,
        // and clear the default when focus is on a non-button (combo/edit/tab
        // strip). Guarded by the comparison so idle iterations don't repaint.
        {
            HWND wantDef = isPushButton(focus) ? focus : nullptr;
            if (wantDef != defBtn) {
                if (defBtn && IsWindow(defBtn)) {
                    SendMessageW(defBtn, BM_SETSTYLE,
                                 static_cast<WPARAM>(BS_PUSHBUTTON), TRUE);
                }
                if (wantDef) {
                    SendMessageW(wantDef, BM_SETSTYLE,
                                 static_cast<WPARAM>(BS_DEFPUSHBUTTON), TRUE);
                }
                defBtn = wantDef;
            }
        }

        // Reveal keyboard focus cues (the dashed ring) on Tab/Shift+Tab. Our
        // custom Tab handling below consumes some Tab keys with `continue`,
        // bypassing IsDialogMessageW -- and that call normally clears
        // UISF_HIDEFOCUS for a freshly opened mouse-activated window. Arrow
        // keys fall through to IsDialogMessageW and reveal the ring on their
        // own, so they don't need explicit handling here.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            SendMessageW(hwnd, WM_CHANGEUISTATE,
                         MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
        }

        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB && panel) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            // Startup state: nothing focused yet. Backward navigation
            // (Shift+Tab) and arrow keys can't resolve a target from a null
            // focus, so the first such key would be a no-op. Anchor focus on
            // the tab strip so the very first Tab OR Shift+Tab lands there.
            if (!focusOnStrip && !focusInPanel) {
                SetFocus(st.hTab);
                continue;  // consume the key
            }
            // Resolve the panel's first/last tabstop robustly. Passing NULL
            // with bPrevious=TRUE to GetNextDlgTabItem is unreliable, so we
            // get the first tabstop forward, then ask for the one *before*
            // it, which wraps around to the genuine last tabstop.
            HWND first = GetNextDlgTabItem(panel, nullptr, FALSE);
            HWND last  = first ? GetNextDlgTabItem(panel, first, TRUE) : nullptr;
            if (focusOnStrip) {
                // Leaving the strip: enter the panel at its first tabstop
                // (or last, with Shift) so the loop reads
                // strip -> c1 -> ... -> cn -> strip and the exact reverse.
                HWND target = shift ? last : first;
                if (target) {
                    SetFocus(target);
                    continue;  // consume the Tab
                }
            } else if (focusInPanel) {
                // Wrapping past either end of the panel returns to the strip
                // rather than cycling around inside the panel.
                if ((!shift && focus == last) || (shift && focus == first)) {
                    SetFocus(st.hTab);
                    continue;  // consume the Tab
                }
                // Interior Tab moves fall through to the panel pump below.
            }
        }

        // Scope the dialog pump to the focused container. With focus inside
        // the panel we pump the panel so arrow/group/mnemonic navigation
        // stays within it; otherwise the main window handles it and the strip
        // keeps native arrow tab-switching (it returns DLGC_WANTARROWS).
        HWND dlgRoot = focusInPanel ? panel : hwnd;
        if (!IsDialogMessageW(dlgRoot, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Make sure the bridge worker is gone before main() returns.
    st.bridge->Stop();
    return static_cast<int>(msg.wParam);
}

namespace {

void TabAreaToRect(HWND hTab, RECT& outDisplay) {
    GetClientRect(hTab, &outDisplay);
    TabCtrl_AdjustRect(hTab, FALSE, &outDisplay);
}

void LayoutPanels(AppState* st) {
    if (!st->hTab) return;
    RECT tabRect;
    GetClientRect(st->hMain, &tabRect);
    // Leave a small margin around the tab control.
    int m = Scale(st->hMain, 8);
    SetWindowPos(st->hTab, nullptr,
                 m, m,
                 tabRect.right - 2 * m,
                 tabRect.bottom - 2 * m,
                 SWP_NOZORDER);

    // Panels are children of the tab control, so use tab-local coordinates
    // returned by TabCtrl_AdjustRect (the "display rectangle" inside the tab
    // body, excluding the tab strip and themed border).
    RECT disp;
    TabAreaToRect(st->hTab, disp);
    int pw = disp.right - disp.left;
    int ph = disp.bottom - disp.top;
    for (HWND p : st->panels) {
        if (p) {
            SetWindowPos(p, nullptr, disp.left, disp.top, pw, ph, SWP_NOZORDER);
        }
    }
}

void OnCreate(HWND hwnd, AppState* st) {
    st->hMain = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

    // Hook the logger to push live updates through WM_APP_LOG_PUSHED.
    Logger::Instance().SetNotifyWindow(hwnd, WM_APP_LOG_PUSHED);

    // Tab control parented to the main window. WS_TABSTOP makes the tab
    // strip a stop in the focus cycle: once focused, the user switches tabs
    // with Left/Right arrows (handled natively by SysTabControl32). We do
    // NOT set WS_EX_CONTROLPARENT here -- it would override WS_TABSTOP and
    // cause IsDialogMessageW to descend into the active panel rather than
    // letting focus park on the tab strip.
    st->hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
                                   WS_TABSTOP | TCS_TABS | TCS_FOCUSONBUTTONDOWN,
                               0, 0, 100, 100, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TAB_MAIN)),
                               st->hInstance, nullptr);
    SendMessageW(st->hTab, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetUiFont()), MAKELPARAM(TRUE, 0));

    TCITEMW tci{};
    tci.mask = TCIF_TEXT;
    tci.pszText = const_cast<wchar_t*>(L"Bridge");
    TabCtrl_InsertItem(st->hTab, 0, &tci);
    tci.pszText = const_cast<wchar_t*>(L"Log");
    TabCtrl_InsertItem(st->hTab, 1, &tci);
    tci.pszText = const_cast<wchar_t*>(L"About");
    TabCtrl_InsertItem(st->hTab, 2, &tci);

    // Build child panels (one per tab). Parented to the *tab control* — the
    // canonical Win32 property-sheet pattern. The panel WndProcs forward
    // WM_COMMAND/WM_NOTIFY to the root window so existing handlers still fire.
    // Parenting panels outside the tab control lets the tab control's themed
    // body paint over them, producing empty-looking tabs.
    st->panels[0] = CreateBridgeTab(st, st->hTab);
    st->panels[1] = CreateLogTab(st, st->hTab);
    st->panels[2] = CreateAboutTab(st, st->hTab);

    LayoutPanels(st);

    // Show only the first panel initially.
    ShowWindow(st->panels[0], SW_SHOW);
    ShowWindow(st->panels[1], SW_HIDE);
    ShowWindow(st->panels[2], SW_HIDE);

    // Initial device scan + log rehydrate so the GUI has data on first paint.
    RescanDevices(st);
    RehydrateLogControl(st);

    // No explicit initial SetFocus call is needed: with WS_EX_CONTROLPARENT on
    // the top-level window and the visible panel, IsDialogMessageW discovers
    // the first tabstop on the first Tab keypress.

    // Periodic refresh for the bridge state label / tray tooltip.
    SetTimer(hwnd, kStateTimerId, kStateTimerMs, nullptr);
}

void OnSize(AppState* st) {
    LayoutPanels(st);
}

void OnTabSelChange(AppState* st) {
    int sel = TabCtrl_GetCurSel(st->hTab);
    for (int i = 0; i < 3; ++i) {
        if (!st->panels[i]) continue;
        ShowWindow(st->panels[i], (i == sel) ? SW_SHOW : SW_HIDE);
        // Only the visible panel keeps WS_EX_CONTROLPARENT. If hidden panels
        // also have it, IsDialogMessageW recurses into them while looking
        // for the next tabstop on Tab key, finds no focusable target, and
        // hangs. Toggling the flag per panel makes Tab cycle inside the
        // visible panel only.
        LONG_PTR ex = GetWindowLongPtrW(st->panels[i], GWL_EXSTYLE);
        if (i == sel) ex |=  WS_EX_CONTROLPARENT;
        else          ex &= ~WS_EX_CONTROLPARENT;
        SetWindowLongPtrW(st->panels[i], GWL_EXSTYLE, ex);
    }
    // Force a redraw of the showing panel so combos/groups paint fully on
    // first activation. This is part of the "invisible until hover"
    // mitigation: combos that just had their parent hidden sometimes skip
    // the next WM_PAINT cycle without a kick.
    if (sel >= 0 && sel < 3 && st->panels[sel]) {
        InvalidateRect(st->panels[sel], nullptr, TRUE);
        UpdateWindow(st->panels[sel]);
    }

    // Log tab (index 1) just became visible: re-tail if auto-scroll is on.
    // Flushes that arrived while it was hidden didn't commit their scroll on
    // the non-visible edit, so without this the view sits at the oldest line
    // until the next live append.
    if (sel == 1) LogTabOnShow(st);

    // No explicit focus anchor is needed on tab switches: with
    // WS_EX_CONTROLPARENT on the main window, the tab control, and the active
    // panel, IsDialogMessageW handles Tab navigation correctly.
}

void OnStateTimer(AppState* st) {
    if (!st->bridge) return;
    BridgeState s = st->bridge->State();
    if (s != st->lastSeenState) {
        st->lastSeenState = s;
        RefreshBridgeTabState(st);
        TrayUpdateTooltip(st);
    }
}

void OnLogPushed(AppState* st) {
    // Coalesce frequent log pushes into a single edit-control update by
    // setting a one-shot timer; if more lines arrive before it fires they
    // get drained in the same flush.
    if (st->logFlushPending) return;
    st->logFlushPending = true;
    SetTimer(st->hMain, kLogFlushTimerId, kLogFlushMs, nullptr);
}

bool ConfirmExitWhileRunning(AppState* st) {
    if (!st->bridge || !st->bridge->IsRunning()) return true;
    int rc = MessageBoxW(st->hMain,
                         L"The bridge is still running.\n"
                         L"Stop it and exit?",
                         L"WASAPI Bridge",
                         MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
    return rc == IDYES;
}

void HandleClose(AppState* st) {
    if (!ConfirmExitWhileRunning(st)) {
        return; // user cancelled
    }
    // Stop the worker without risking an indefinite hang on exit. Normally
    // the worker stops well within the timeout. If it doesn't -- e.g. wedged
    // inside a driver-level WASAPI init deadlock -- TryGracefulStop returns
    // false and we force-terminate rather than freeze the GUI on a join().
    // The OS reclaims the stuck thread and all process resources; the audio
    // service detects the dead client and cleans up its side.
    if (st->bridge && !st->bridge->TryGracefulStop(3000)) {
        WB_LOG_WARN("Worker did not stop in time; forcing exit.");
        TrayHide(st);
        ExitProcess(2);
    }
    TrayHide(st);
    DestroyWindow(st->hMain);
}

void HandleSysCommand(AppState* st, WPARAM cmd) {
    if ((cmd & 0xFFF0) == SC_MINIMIZE && st->minimizeToTray) {
        // Hide-to-tray instead of plain minimise.
        ShowWindow(st->hMain, SW_HIDE);
        TrayShow(st);
        return;
    }
    DefWindowProcW(st->hMain, WM_SYSCOMMAND, cmd, 0);
}

void RestoreFromTray(AppState* st) {
    ShowWindow(st->hMain, SW_SHOW);
    if (IsIconic(st->hMain)) {
        ShowWindow(st->hMain, SW_RESTORE);
    }
    SetForegroundWindow(st->hMain);
    TrayHide(st);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* st = GetAppState(hwnd);

    switch (msg) {
        case WM_NCCREATE: {
            // Stash the AppState pointer passed via CreateWindowEx lpCreateParams.
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        case WM_CREATE:
            OnCreate(hwnd, st);
            return 0;

        case WM_SIZE:
            if (st) OnSize(st);
            return 0;

        case WM_GETMINMAXINFO: {
            // Cap the minimum size so users can't shrink below a point where
            // the Bridge tab's group boxes start clipping into each other.
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            int minW = ScaleSystem(480);
            int minH = ScaleSystem(320);
            RECT r{0, 0, minW, minH};
            AdjustWindowRectEx(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
            mmi->ptMinTrackSize.x = r.right - r.left;
            mmi->ptMinTrackSize.y = r.bottom - r.top;
            return 0;
        }

        case WM_NOTIFY: {
            if (!st) break;
            auto* hdr = reinterpret_cast<NMHDR*>(lp);
            if (hdr->hwndFrom == st->hTab && hdr->code == TCN_SELCHANGE) {
                OnTabSelChange(st);
                return 0;
            }
            if (HandleAboutNotify(st, hdr)) return 0;
            break;
        }

        case WM_COMMAND: {
            if (!st) break;
            WORD ctrlId    = LOWORD(wp);
            WORD notifyCode = HIWORD(wp);
            if (ctrlId == IDM_TRAY_OPEN) {
                RestoreFromTray(st);
                return 0;
            }
            if (ctrlId == IDM_TRAY_EXIT) {
                HandleClose(st);
                return 0;
            }
            if (HandleBridgeCommand(st, ctrlId, notifyCode)) return 0;
            if (HandleLogCommand(st, ctrlId, notifyCode))    return 0;
            if (HandleAboutCommand(st, ctrlId, notifyCode))  return 0;
            break;
        }

        case WM_TIMER:
            if (!st) break;
            if (wp == kStateTimerId) {
                OnStateTimer(st);
                return 0;
            }
            if (wp == kLogFlushTimerId) {
                KillTimer(hwnd, kLogFlushTimerId);
                FlushLogToControl(st);
                return 0;
            }
            break;

        case WM_APP_LOG_PUSHED:
            if (st) OnLogPushed(st);
            return 0;

        case WM_APP_TRAYICON: {
            if (!st) break;
            UINT event = LOWORD(lp);
            if (event == WM_LBUTTONDBLCLK || event == WM_LBUTTONUP) {
                RestoreFromTray(st);
            } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                POINT pt;
                GetCursorPos(&pt);
                TrayPopupMenu(st, pt);
            }
            return 0;
        }

        case WM_SYSCOMMAND:
            if (st) {
                HandleSysCommand(st, wp);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (st) {
                HandleClose(st);
                return 0;
            }
            break;

        case WM_DESTROY:
            if (st) {
                Logger::Instance().SetNotifyWindow(nullptr, 0);
                TrayHide(st);
                KillTimer(hwnd, kStateTimerId);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

} // namespace wb
