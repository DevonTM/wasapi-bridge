#ifndef WB_GUI_UI_UTILS_H
#define WB_GUI_UI_UTILS_H

#include <string>
#include <windows.h>

namespace wb {

// DPI scaling: returns the value in pixels for the given window's DPI
// (Win10+; 96 fallback on older systems).
int Scale(HWND hwnd, int value);

// Same as Scale() but reads the system DPI -- useful for top-level window
// sizing computed before we have an HWND.
int ScaleSystem(int value);

// The font we use across the entire UI. Cached after first call.
HFONT GetUiFont();
HFONT GetUiFontBold();
HFONT GetMonoFont();

// Convert UTF-8 to a wide string (Win32 API).
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

// Apply our default font to a control (and its descendants are not touched).
void ApplyUiFont(HWND control, HFONT font = nullptr);

// Recursively apply our default font to every child of a panel. Used after
// we create the panel + controls so we don't have to remember per control.
void ApplyUiFontRecursive(HWND parent);

} // namespace wb

#endif // WB_GUI_UI_UTILS_H
