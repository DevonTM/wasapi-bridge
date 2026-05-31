#include "about_tab.h"

#include <commctrl.h>
#include <shellapi.h>

#include "../version.h"
#include "resource.h"
#include "ui_utils.h"

// Some MinGW commctrl.h versions ship LM_GETIDEALHEIGHT but not the
// LM_GETIDEALSIZE alias (they share message id 0x0701; with a SIZE* lParam
// it fills the ideal width too). Define it if missing.
#ifndef LM_GETIDEALSIZE
#define LM_GETIDEALSIZE LM_GETIDEALHEIGHT
#endif

namespace wb {

namespace {

constexpr const wchar_t* kRepoUrl    = L"https://github.com/DevonTM/wasapi-bridge";
constexpr const wchar_t* kLicenseUrl = L"https://github.com/DevonTM/wasapi-bridge/blob/main/LICENSE";

// Forwarding WndProc (see bridge_tab.cpp).
LRESULT CALLBACK AboutPanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND || msg == WM_NOTIFY) {
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root && root != hwnd) return SendMessageW(root, msg, wp, lp);
    }
    if (msg == WM_SIZE) {
        AppState* st = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (st) LayoutAboutTab(st, LOWORD(lp), HIWORD(lp));
    }
    if (msg == WM_CTLCOLORSTATIC) {
        // Match the white panel background.
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

HWND CreateAboutTab(AppState* st, HWND hParent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style  = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = AboutPanelProc;
        wc.hInstance   = st->hInstance;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"WBAboutPanel";
        RegisterClassExW(&wc);
        registered = true;
    }

    HWND panel = CreateWindowExW(WS_EX_CONTROLPARENT, L"WBAboutPanel", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS,
                                 0, 0, 100, 100, hParent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ABOUT_PANEL)),
                                 st->hInstance, nullptr);
    // Stash AppState* so AboutPanelProc can route WM_SIZE -> LayoutAboutTab.
    SetWindowLongPtrW(panel, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));

    const int M = 24;
    int y = M + 8;

    // App icon (large) -- LoadImageW with LR_SHARED keeps the resource cached.
    HICON hIcon = reinterpret_cast<HICON>(LoadImageW(st->hInstance,
                                                     MAKEINTRESOURCEW(IDI_APP_ICON),
                                                     IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR | LR_SHARED));
    // WS_GROUP on the first child bounds the non-focusable header block
    // (icon + name + version) as its own group, so the link/button group
    // below is the only arrow-navigable cluster. Positions here are
    // placeholders; LayoutAboutTab centers everything on the first WM_SIZE.
    st->hStaticIcon = CreateWindowExW(0, L"STATIC", L"",
                                      WS_CHILD | WS_VISIBLE | WS_GROUP | SS_ICON | SS_REALSIZECONTROL,
                                      M, y, 64, 64, panel,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATIC_ICON)),
                                      st->hInstance, nullptr);
    SendMessageW(st->hStaticIcon, STM_SETICON, reinterpret_cast<WPARAM>(hIcon), 0);

    // App name (bold, larger). SS_CENTER so the text self-centers in its
    // full-width rect (set by LayoutAboutTab).
    HWND lblName = CreateWindowExW(0, L"STATIC", L"WASAPI Bridge",
                                   WS_CHILD | WS_VISIBLE | SS_CENTER,
                                   M, y, 380, 26, panel,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_APP_NAME)),
                                   st->hInstance, nullptr);
    LOGFONTW lf{};
    HFONT base = GetUiFontBold();
    GetObjectW(base, sizeof(lf), &lf);
    lf.lfHeight = -16;
    HFONT title = CreateFontIndirectW(&lf);
    SendMessageW(lblName, WM_SETFONT, reinterpret_cast<WPARAM>(title), MAKELPARAM(TRUE, 0));

    // Version line (regular weight, centered).
    wchar_t verBuf[64];
    _snwprintf_s(verBuf, _TRUNCATE, L"Version %hs", WB_VERSION);
    HWND lblVer = CreateWindowExW(0, L"STATIC", verBuf,
                                  WS_CHILD | WS_VISIBLE | SS_CENTER,
                                  M, y, 380, 20, panel,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LBL_APP_VERSION)),
                                  st->hInstance, nullptr);
    ApplyUiFont(lblVer);

    // (Tagline/short description intentionally dropped from the About tab.)

    // SysLink to license.
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    wchar_t linkText[256];
    _snwprintf_s(linkText, _TRUNCATE,
                 L"Licensed under the <a href=\"%s\">MIT License</a>",
                 kLicenseUrl);
    // WS_GROUP starts the [license link, GitHub button] group so arrow keys
    // cycle between the two focusable controls instead of dead-ending.
    HWND link = CreateWindowExW(0, WC_LINK, linkText,
                                WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | LWS_TRANSPARENT,
                                M, y, 420, 22, panel,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LNK_LICENSE)),
                                st->hInstance, nullptr);
    ApplyUiFont(link);
    y += 32;

    // GitHub button. WS_GROUP makes it its own one-control group so arrow
    // keys do nothing here (matching the isolated license link above); the
    // two are reached via Tab/Shift+Tab only.
    HWND btn = CreateWindowExW(0, L"BUTTON", L"Open GitHub repository",
                               WS_CHILD | WS_VISIBLE | WS_GROUP | WS_TABSTOP | BS_PUSHBUTTON,
                               M, y, 220, 30, panel,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BTN_GITHUB)),
                               st->hInstance, nullptr);
    ApplyUiFont(btn);

    return panel;
}

namespace {

// Ask a SysLink for the rendered width of its *visible* text. We can't use
// GetWindowText + GetTextExtent here: a SysLink's window text is the raw
// markup including the full href URL, which measures far too wide. The
// control's own LM_GETIDEALSIZE (comctl32 v6) strips the markup and returns
// the true display size given a max width.
int LinkIdealWidth(HWND hLink, int maxWidth) {
    SIZE sz{};
    if (SendMessageW(hLink, LM_GETIDEALSIZE, static_cast<WPARAM>(maxWidth),
                     reinterpret_cast<LPARAM>(&sz)) && sz.cx > 0) {
        return sz.cx;
    }
    return 0;
}

} // namespace

void LayoutAboutTab(AppState* st, int panelW, int panelH) {
    if (panelW <= 0 || panelH <= 0) return;

    HWND lblName = GetDlgItem(st->hStaticIcon ? GetParent(st->hStaticIcon) : nullptr,
                              IDC_LBL_APP_NAME);
    HWND panel   = GetParent(st->hStaticIcon);
    HWND lblVer  = GetDlgItem(panel, IDC_LBL_APP_VERSION);
    HWND link    = GetDlgItem(panel, IDC_LNK_LICENSE);
    HWND btn     = GetDlgItem(panel, IDC_BTN_GITHUB);

    // Per-element sizes.
    const int iconSz = 64;
    const int nameH  = 26;
    const int verH   = 20;
    const int linkH  = 22;
    const int btnW   = 220;
    const int btnH   = 30;

    // Vertical gaps between stack items (compact 64px layout). gNameVer/
    // gVerLink are biased so the version sits a touch lower, closer to the
    // license line (sum unchanged, so the rest of the stack doesn't shift).
    const int gIconName = 14;
    const int gNameVer  = 6;
    const int gVerLink  = 6;
    const int gLinkBtn  = 10;

    int stackH = iconSz + gIconName + nameH + gNameVer + verH +
                 gVerLink + linkH + gLinkBtn + btnH;

    // Center the whole block vertically (clamp to a small top margin).
    int top = (panelH - stackH) / 2;
    if (top < 12) top = 12;

    auto centerX = [panelW](int w) { return (panelW - w) / 2; };

    int y = top;
    // Icon.
    SetWindowPos(st->hStaticIcon, nullptr, centerX(iconSz), y, iconSz, iconSz,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    y += iconSz + gIconName;

    // Name + version use SS_CENTER, so give them the full content width and
    // let the text center itself.
    int textW = panelW - 2 * 12;
    SetWindowPos(lblName, nullptr, 12, y, textW, nameH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    y += nameH + gNameVer;
    SetWindowPos(lblVer, nullptr, 12, y, textW, verH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    y += verH + gVerLink;

    // SysLink can't use SS_CENTER; center it by its ideal (visible) width.
    int linkW = LinkIdealWidth(link, textW);
    if (linkW <= 0 || linkW > textW) linkW = textW;
    SetWindowPos(link, nullptr, centerX(linkW), y, linkW, linkH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    y += linkH + gLinkBtn;

    // GitHub button: fixed width, centered.
    SetWindowPos(btn, nullptr, centerX(btnW), y, btnW, btnH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    InvalidateRect(panel, nullptr, TRUE);
}

bool HandleAboutNotify(AppState* /*st*/, NMHDR* hdr) {
    if (!hdr) return false;
    if (hdr->idFrom == IDC_LNK_LICENSE &&
        (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
        auto* link = reinterpret_cast<NMLINK*>(hdr);
        const wchar_t* url = link->item.szUrl[0] ? link->item.szUrl : kLicenseUrl;
        ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    return false;
}

bool HandleAboutCommand(AppState* /*st*/, WORD ctrlId, WORD notifyCode) {
    if (ctrlId == IDC_BTN_GITHUB && notifyCode == BN_CLICKED) {
        ShellExecuteW(nullptr, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return true;
    }
    return false;
}

} // namespace wb
