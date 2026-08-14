#ifndef WB_GUI_UI_UTILS_H
#define WB_GUI_UI_UTILS_H

#include <string>
#include <utility>
#include <windows.h>

namespace wb {

struct DestroyMenuHandle {
    void operator()(HMENU handle) const { DestroyMenu(handle); }
};
struct DeleteGdiHandle {
    void operator()(HGDIOBJ handle) const { DeleteObject(handle); }
};

template <typename T, typename Deleter>
class UniqueWin32Handle {
public:
    UniqueWin32Handle() = default;
    explicit UniqueWin32Handle(T handle) : handle_(handle) {}
    ~UniqueWin32Handle() { reset(); }
    UniqueWin32Handle(const UniqueWin32Handle&) = delete;
    UniqueWin32Handle& operator=(const UniqueWin32Handle&) = delete;
    UniqueWin32Handle(UniqueWin32Handle&& other) noexcept : handle_(other.release()) {}
    UniqueWin32Handle& operator=(UniqueWin32Handle&& other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    T get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    T release() { T handle = handle_; handle_ = nullptr; return handle; }
    void reset(T handle = nullptr) {
        if (handle_) Deleter{}(handle_);
        handle_ = handle;
    }
private:
    T handle_ = nullptr;
};

using UniqueMenu = UniqueWin32Handle<HMENU, DestroyMenuHandle>;
using UniqueFont = UniqueWin32Handle<HFONT, DeleteGdiHandle>;

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

int ShowInfo(HWND owner, const wchar_t* message, const wchar_t* title);
int ShowWarning(HWND owner, const wchar_t* message, const wchar_t* title);
int ShowError(HWND owner, const wchar_t* message, const wchar_t* title);
int ShowQuestion(HWND owner, const wchar_t* message, const wchar_t* title,
                 UINT buttons = MB_YESNO | MB_DEFBUTTON2);

// Recursively apply our default font to every child of a panel. Used after
// we create the panel + controls so we don't have to remember per control.
void ApplyUiFontRecursive(HWND parent);

} // namespace wb

#endif // WB_GUI_UI_UTILS_H
