#ifndef WB_GUI_ABOUT_TAB_H
#define WB_GUI_ABOUT_TAB_H

#include <windows.h>

#include "app_state.h"

namespace wb {

HWND CreateAboutTab(AppState* st, HWND hParent);

// Reflow the About tab controls to the given panel client size (centers the
// icon / name / version / license / button stack).
void LayoutAboutTab(AppState* st, int panelW, int panelH);

bool HandleAboutNotify(AppState* st, NMHDR* hdr);
bool HandleAboutCommand(AppState* st, WORD ctrlId, WORD notifyCode);

} // namespace wb

#endif // WB_GUI_ABOUT_TAB_H
