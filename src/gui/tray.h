#ifndef WB_GUI_TRAY_H
#define WB_GUI_TRAY_H

#include <windows.h>

#include "app_state.h"

namespace wb {

// Add the tray icon (idempotent if already visible).
void TrayShow(AppState* st);

// Remove the tray icon.
void TrayHide(AppState* st);

// Update the tray tooltip with the current state. No-op when tray hidden.
void TrayUpdateTooltip(AppState* st);

// Show the right-click context menu (Open / Exit). Caller passes screen-pt.
void TrayPopupMenu(AppState* st, POINT screenPt);

} // namespace wb

#endif // WB_GUI_TRAY_H
