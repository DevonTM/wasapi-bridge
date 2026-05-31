#include "log_tab.h"

#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include "logger.h"
#include "resource.h"
#include "ui_utils.h"

namespace wb {

namespace {

// Forwarding WndProc; see bridge_tab.cpp for the rationale. Panels are
// children of the tab control so notifications must be bubbled up to the
// top-level window.
LRESULT CALLBACK LogPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root && root != hwnd) return SendMessageW(root, msg, wp, lp);
    }
    if (msg == WM_SIZE) {
        AppState* st = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (st) LayoutLogTab(st, LOWORD(lp), HIWORD(lp));
    }
    if (msg == WM_CTLCOLORSTATIC) {
        // Paint on an OPAQUE white background. We deliberately do NOT use
        // SetBkMode(TRANSPARENT) here: the read-only log EDIT is a single
        // color control, and with a transparent background ClearType
        // sub-pixel anti-aliasing has no clean backdrop to composite against,
        // so the red/green/teal fringes accumulate across repeated repaints
        // and the text appears to thicken / rainbow over time. Forcing an
        // opaque white background makes every glyph repaint cleanly.
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ScrollToBottom(HWND hEdit) {
    // Move caret to the last position and scroll caret into view.
    int len = GetWindowTextLengthW(hEdit);
    SendMessageW(hEdit, EM_SETSEL, static_cast<WPARAM>(len), static_cast<LPARAM>(len));
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
}

std::wstring BuildAppendBuffer(const std::vector<LogLine>& lines) {
    std::wstring out;
    out.reserve(lines.size() * 80);
    for (const auto& l : lines) {
        std::string formatted = Logger::Format(l);
        std::wstring w = Utf8ToWide(formatted);
        out.append(w);
        out.append(L"\r\n");
    }
    return out;
}

std::wstring BuildExportTimestampedName() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    wchar_t buf[64];
    _snwprintf_s(buf, _TRUNCATE,
                 L"wasapi-bridge-log-%04d%02d%02d-%02d%02d%02d.txt",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::wstring(buf);
}

void DoExport(AppState* st) {
    wchar_t fileBuf[MAX_PATH];
    std::wstring suggested = BuildExportTimestampedName();
    wcscpy_s(fileBuf, suggested.c_str());

    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = st->hMain;
    ofn.lpstrFilter     = L"Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile       = fileBuf;
    ofn.nMaxFile        = MAX_PATH;
    ofn.lpstrDefExt     = L"txt";
    ofn.Flags           = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle      = L"Export log";

    if (!GetSaveFileNameW(&ofn)) {
        return;
    }

    auto lines = Logger::Instance().Snapshot();
    std::ofstream out(fileBuf, std::ios::binary);
    if (!out) {
        MessageBoxW(st->hMain, L"Failed to open file for writing.",
                    L"Export log", MB_OK | MB_ICONERROR);
        return;
    }
    // Plain UTF-8, no BOM. The log content is ASCII/UTF-8 already, so a BOM
    // is unnecessary and some tools display it as stray characters.
    for (const auto& l : lines) {
        std::string formatted = Logger::Format(l);
        out.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
        out.write("\r\n", 2);
    }
    WB_LOG_INFO("Log exported to %ls", fileBuf);
}

} // namespace

HWND CreateLogTab(AppState* st, HWND hParent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style  = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = LogPanelProc;
        wc.hInstance   = st->hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"WBLogPanel";
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND panel = CreateWindowExW(WS_EX_CONTROLPARENT, L"WBLogPanel", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS,
                                 0, 0, 100, 100, hParent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOG_PANEL)),
                                 st->hInstance, nullptr);
    SetWindowLongPtrW(panel, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

    const int M = 12;
    const int btnH = 26;
    const int rowH = 24;

    // Edit fills most of the area; bottom row hosts checkbox + export button.
    // WS_GROUP fences the (non-tabstop) read-only edit into its own arrow
    // group so Up/Left from the auto-scroll checkbox can't wrap into it and
    // trap the focus on the log caret.
    st->hEdtLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_CHILD | WS_VISIBLE | WS_GROUP | WS_VSCROLL |
                                      ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY |
                                      ES_NOHIDESEL,
                                  M, M, 540 - 2*M, 320,
                                  panel,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDT_LOG)),
                                  st->hInstance, nullptr);
    SendMessageW(st->hEdtLog, WM_SETFONT, reinterpret_cast<WPARAM>(GetMonoFont()), MAKELPARAM(TRUE, 0));
    // Allow the edit to hold up to ~1.5 MB so the 5000-line cap fits comfortably.
    SendMessageW(st->hEdtLog, EM_LIMITTEXT, 1500000, 0);

    // WS_GROUP starts the bottom-row group [auto-scroll, Clear, Export] so
    // arrow keys cycle only those three and never fall into the log edit.
    int rowY = M + 320 + 8;
    st->hChkAutoScroll = CreateWindowExW(0, L"BUTTON", L"Auto-scroll",
                                         WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         M, rowY + 2, 120, rowH,
                                         panel,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CHK_AUTOSCROLL)),
                                         st->hInstance, nullptr);
    SendMessageW(st->hChkAutoScroll, BM_SETCHECK, BST_CHECKED, 0);

    st->hBtnClear = CreateWindowExW(0, L"BUTTON", L"Clear",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    540 - M - 84 - 8 - 84, rowY, 84, btnH,
                                    panel,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_CLEAR_LOG)),
                                    st->hInstance, nullptr);
    st->hBtnExport = CreateWindowExW(0, L"BUTTON", L"Export",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     540 - M - 84, rowY, 84, btnH,
                                     panel,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_EXPORT_LOG)),
                                     st->hInstance, nullptr);

    ApplyUiFont(st->hChkAutoScroll);
    ApplyUiFont(st->hBtnClear);
    ApplyUiFont(st->hBtnExport);

    return panel;
}

void RehydrateLogControl(AppState* st) {
    auto lines = Logger::Instance().Snapshot();
    std::wstring buf = BuildAppendBuffer(lines);
    SetWindowTextW(st->hEdtLog, buf.c_str());
    if (SendMessageW(st->hChkAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        ScrollToBottom(st->hEdtLog);
    }
}

void LogTabOnShow(AppState* st) {
    // Re-tail when the Log tab is shown. The flush timer is global and keeps
    // appending while this tab is hidden, but EM_SCROLLCARET doesn't commit on
    // a non-visible edit, so the view can be stuck at the oldest line. Now that
    // the edit is visible, ScrollToBottom settles correctly. Only when
    // auto-scroll is on -- with it off we leave the view where the user left it.
    if (SendMessageW(st->hChkAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        ScrollToBottom(st->hEdtLog);
    }
}

void FlushLogToControl(AppState* st) {
    auto lines = Logger::Instance().DrainNew();
    if (lines.empty()) {
        st->logFlushPending = false;
        return;
    }

    // The Auto-scroll checkbox is the single source of truth: checked means
    // always tail the latest line (even if the user had scrolled up),
    // unchecked means leave the view exactly where it is. We intentionally do
    // NOT also gate on "is the view at the bottom" -- that made checked
    // auto-scroll stop following after any manual scroll-up.
    bool follow        = SendMessageW(st->hChkAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring chunk = BuildAppendBuffer(lines);

    // Capture the pre-append view + selection. EM_SETSEL(end)+EM_REPLACESEL
    // below force the caret (and thus the view) to the bottom; when we are
    // NOT following the tail we must put them back, otherwise the console
    // snaps to the latest line even with auto-scroll off or while the user
    // has scrolled up to read.
    DWORD selStart = 0, selEnd = 0;
    SendMessageW(st->hEdtLog, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selStart),
                 reinterpret_cast<LPARAM>(&selEnd));
    int firstVisibleBefore =
        static_cast<int>(SendMessageW(st->hEdtLog, EM_GETFIRSTVISIBLELINE, 0, 0));

    int len = GetWindowTextLengthW(st->hEdtLog);

    if (follow) {
        // Tailing the latest line. EM_REPLACESEL leaves the view at the
        // bottom, which is exactly where we want it -- no scroll-back, so no
        // flicker. We must NOT freeze redraw here: under WM_SETREDRAW FALSE
        // the edit's scroll math (EM_SCROLLCARET) doesn't settle on the
        // bottom reliably, which previously broke auto-scroll following.
        SendMessageW(st->hEdtLog, EM_SETSEL,
                     static_cast<WPARAM>(len), static_cast<LPARAM>(len));
        SendMessageW(st->hEdtLog, EM_REPLACESEL, FALSE,
                     reinterpret_cast<LPARAM>(chunk.c_str()));
        ScrollToBottom(st->hEdtLog);
    } else {
        // Not following: EM_REPLACESEL scrolls to the bottom synchronously
        // (an internal ScrollWindow that repaints immediately), then we
        // scroll back to the user's position. Freeze painting across the
        // append + restore so the bottom never flashes; one InvalidateRect
        // afterwards paints the final (restored) state once. EM_LINESCROLL
        // updates position correctly even with redraw disabled.
        SendMessageW(st->hEdtLog, WM_SETREDRAW, FALSE, 0);
        SendMessageW(st->hEdtLog, EM_SETSEL,
                     static_cast<WPARAM>(len), static_cast<LPARAM>(len));
        SendMessageW(st->hEdtLog, EM_REPLACESEL, FALSE,
                     reinterpret_cast<LPARAM>(chunk.c_str()));
        SendMessageW(st->hEdtLog, EM_SETSEL,
                     static_cast<WPARAM>(selStart), static_cast<LPARAM>(selEnd));
        int firstVisibleNow =
            static_cast<int>(SendMessageW(st->hEdtLog, EM_GETFIRSTVISIBLELINE, 0, 0));
        int delta = firstVisibleBefore - firstVisibleNow;
        if (delta != 0) {
            SendMessageW(st->hEdtLog, EM_LINESCROLL, 0, static_cast<LPARAM>(delta));
        }
        SendMessageW(st->hEdtLog, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(st->hEdtLog, nullptr, TRUE);
    }
    st->logFlushPending = false;
}

bool HandleLogCommand(AppState* st, WORD ctrlId, WORD notifyCode) {
    switch (ctrlId) {
        case IDC_BTN_EXPORT_LOG:
            if (notifyCode == BN_CLICKED) {
                DoExport(st);
                return true;
            }
            break;
        case IDC_BTN_CLEAR_LOG:
            if (notifyCode == BN_CLICKED) {
                Logger::Instance().Clear();
                SetWindowTextW(st->hEdtLog, L"");
                return true;
            }
            break;
        case IDC_CHK_AUTOSCROLL:
            if (notifyCode == BN_CLICKED) {
                if (SendMessageW(st->hChkAutoScroll, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    ScrollToBottom(st->hEdtLog);
                }
                return true;
            }
            break;
    }
    return false;
}

void LayoutLogTab(AppState* st, int panelW, int panelH) {
    if (panelW <= 0 || panelH <= 0) return;
    const int M = 12;
    const int btnH = 26;
    const int rowH = 24;
    // 24 (was 32): reserving less space at the panel bottom makes the edit
    // taller. Since the button row Y reduces to M + editH + editButtonGap,
    // and editH already subtracts editButtonGap, the row position (and thus
    // the bottom gap) depends only on bottomBarH -> bottom gap stays 13px.
    const int bottomBarH = 24;
    // Space between the console edit and the button row. Rendered gap is this
    // + 1px from the edit's client-edge border, so 12 -> ~13px (matching the
    // bottom gap). Drop to 10 for ~11px if 13 looks too airy.
    const int editButtonGap = 12;

    int editH = panelH - 2*M - bottomBarH - editButtonGap;
    if (editH < 60) editH = 60;
    SetWindowPos(st->hEdtLog, nullptr,
                 M, M, panelW - 2*M, editH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    int rowY = M + editH + editButtonGap;
    SetWindowPos(st->hChkAutoScroll, nullptr,
                 M, rowY + 2, 120, rowH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hBtnClear, nullptr,
                 panelW - M - 84 - 8 - 84, rowY, 84, btnH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(st->hBtnExport, nullptr,
                 panelW - M - 84, rowY, 84, btnH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace wb
