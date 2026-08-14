#include "tray.h"

#include <shellapi.h>
#include <cstdio>

#include "resource.h"
#include "ui_utils.h"

namespace wb {

namespace {

using UniqueMenu = UniqueWin32Handle<HMENU, DestroyMenuHandle>;

constexpr UINT kTrayId = 1;

NOTIFYICONDATAW MakeNid(AppState* st) {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = st->hMain;
    nid.uID              = kTrayId;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYICON;
    nid.hIcon            = st->hIconSmall ? st->hIconSmall : st->hIconLarge;
    return nid;
}

void FillTooltip(AppState* st, NOTIFYICONDATAW& nid) {
    BridgeState s = st->bridge ? st->bridge->State() : BridgeState::Stopped;
    _snwprintf_s(nid.szTip, _countof(nid.szTip), _TRUNCATE,
                 L"WASAPI Bridge \u2014 %hs", BridgeStateLabel(s));
}

} // namespace

void TrayShow(AppState* st) {
    if (st->trayIconVisible) {
        TrayUpdateTooltip(st);
        return;
    }
    NOTIFYICONDATAW nid = MakeNid(st);
    FillTooltip(st, nid);
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        st->trayIconVisible = true;
    }
}

void TrayHide(AppState* st) {
    if (!st->trayIconVisible) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = st->hMain;
    nid.uID    = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    st->trayIconVisible = false;
}

void TrayUpdateTooltip(AppState* st) {
    if (!st->trayIconVisible) return;
    NOTIFYICONDATAW nid = MakeNid(st);
    nid.uFlags = NIF_TIP;
    FillTooltip(st, nid);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayPopupMenu(AppState* st, POINT screenPt) {
    UniqueMenu menu(CreatePopupMenu());
    if (!menu) return;
    AppendMenuW(menu.get(), MF_STRING, IDM_TRAY_OPEN, L"Open WASAPI Bridge");
    AppendMenuW(menu.get(), MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu.get(), MF_STRING, IDM_TRAY_EXIT, L"Exit");
    SetMenuDefaultItem(menu.get(), IDM_TRAY_OPEN, FALSE);

    // Per the docs, you must SetForegroundWindow before TrackPopupMenu and
    // post a dummy message after, otherwise the menu won't dismiss properly
    // when the user clicks outside it.
    SetForegroundWindow(st->hMain);
    TrackPopupMenu(menu.get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   screenPt.x, screenPt.y, 0, st->hMain, nullptr);
    PostMessageW(st->hMain, WM_NULL, 0, 0);
}

} // namespace wb
