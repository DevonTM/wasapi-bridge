#ifndef WB_GUI_LOG_TAB_H
#define WB_GUI_LOG_TAB_H

#include <windows.h>

#include "app_state.h"

namespace wb {

HWND CreateLogTab(AppState* st, HWND hParent);

// Reflow log tab controls to the given panel client size.
void LayoutLogTab(AppState* st, int panelW, int panelH);

// Appends any log lines that have arrived since the last drain. Called from
// the main-window WM_TIMER (kLogFlushTimerId) so we coalesce paint updates.
void FlushLogToControl(AppState* st);

bool HandleLogCommand(AppState* st, WORD ctrlId, WORD notifyCode);

// Reload the entire edit control from the logger snapshot. Called once when
// the GUI starts so existing log lines are visible immediately.
void RehydrateLogControl(AppState* st);

// Called when the Log tab becomes the active tab. If auto-scroll is on, jump
// to the newest line. Flushes that arrive while the tab is hidden don't commit
// their scroll on a non-visible edit, so we re-tail here once it's shown.
void LogTabOnShow(AppState* st);

} // namespace wb

#endif // WB_GUI_LOG_TAB_H
