#ifndef WB_GUI_MAIN_WINDOW_H
#define WB_GUI_MAIN_WINDOW_H

#include <windows.h>

namespace wb {

// Run the GUI message loop. Creates the main window, registers tab pages,
// and pumps messages until the window is destroyed. Returns the WPARAM of
// the final WM_QUIT (suitable as the process exit code).
int RunGui(HINSTANCE hInstance, int nCmdShow);

} // namespace wb

#endif // WB_GUI_MAIN_WINDOW_H
