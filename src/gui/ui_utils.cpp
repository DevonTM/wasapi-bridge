#include "ui_utils.h"

#include <algorithm>
#include <cstring>

namespace wb {

namespace {

using PFN_GetDpiForWindow = UINT (WINAPI*)(HWND);

UINT GetDpiForWindowSafe(HWND hwnd) {
    // GetDpiForWindow is Win10 1607+. Fall back to the system DPI from the
    // device context when not present, which matches the unaware/system-DPI
    // legacy path on Win7/8.
    static auto fn = [] {
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (!u32) return static_cast<PFN_GetDpiForWindow>(nullptr);
        // GCC warns on direct reinterpret_cast from FARPROC to a typed
        // function pointer because the argument signatures differ. Round
        // through void* to silence -Wcast-function-type without losing
        // type information at the call site.
        FARPROC raw = GetProcAddress(u32, "GetDpiForWindow");
        return reinterpret_cast<PFN_GetDpiForWindow>(reinterpret_cast<void*>(raw));
    }();
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi != 0) return dpi;
    }
    HDC hdc = GetDC(nullptr);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
    ReleaseDC(nullptr, hdc);
    return dpi == 0 ? 96 : dpi;
}

HFONT MakeUiFont(int weight) {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        // Last-ditch fallback. Segoe UI ships with Vista+; on Win7 it's there.
        LOGFONTW lf{};
        lf.lfHeight = -12;
        lf.lfWeight = weight;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        return CreateFontIndirectW(&lf);
    }
    LOGFONTW lf = ncm.lfMessageFont;
    lf.lfWeight = weight;
    lf.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&lf);
}

HFONT MakeMonoFont() {
    LOGFONTW lf{};
    lf.lfHeight = -12;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    return CreateFontIndirectW(&lf);
}

} // namespace

int Scale(HWND hwnd, int value) {
    UINT dpi = GetDpiForWindowSafe(hwnd);
    return MulDiv(value, static_cast<int>(dpi), 96);
}

int ScaleSystem(int value) {
    return Scale(nullptr, value);
}

HFONT GetUiFont() {
    static HFONT font = MakeUiFont(FW_NORMAL);
    return font;
}

HFONT GetUiFontBold() {
    static HFONT font = MakeUiFont(FW_SEMIBOLD);
    return font;
}

HFONT GetMonoFont() {
    static HFONT font = MakeMonoFont();
    return font;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

void ApplyUiFont(HWND control, HFONT font) {
    if (!control) return;
    SendMessageW(control, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font ? font : GetUiFont()),
                 MAKELPARAM(TRUE, 0));
}

static BOOL CALLBACK ApplyFontEnumProc(HWND child, LPARAM lParam) {
    HFONT font = reinterpret_cast<HFONT>(lParam);
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), MAKELPARAM(TRUE, 0));
    return TRUE;
}

void ApplyUiFontRecursive(HWND parent) {
    if (!parent) return;
    EnumChildWindows(parent, ApplyFontEnumProc,
                     reinterpret_cast<LPARAM>(GetUiFont()));
}

} // namespace wb
