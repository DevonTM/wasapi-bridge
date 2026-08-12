// Single-binary entry point for WASAPI Bridge.
//
// Compiled with the WINDOWS subsystem so launching from Explorer doesn't open
// a stray console. Launching from a terminal still gets log output: we call
// AttachConsole(ATTACH_PARENT_PROCESS) on startup and re-bind stdout/stderr
// when that succeeds. This keeps "wasapi-bridge.exe" useful as a one-shot
// from PowerShell or cmd while still presenting the GUI for normal users.

#include <windows.h>
#include <cstdio>
#include <io.h>
#include <fcntl.h>

#include "gui/app_state.h"
#include "gui/logger.h"
#include "gui/main_window.h"
#include "types.h"
#include "version.h"

// Global state definitions (used by callbacks/device_manager).
std::atomic<bool>       g_keepRunning{true};
RecoveryState           g_recoveryState;
std::mutex              g_wakeupMutex;
std::condition_variable g_wakeupCv;

namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\WASAPI Bridge";
constexpr wchar_t kMainWindowClass[] = L"WasapiBridgeMain";

void ActivateExistingInstance() {
    HWND hwnd = FindWindowW(kMainWindowClass, L"WASAPI Bridge");
    if (hwnd) PostMessageW(hwnd, wb::WM_APP_ACTIVATE_EXISTING, 0, 0);
}

// Try to re-attach stdout/stderr to the launching console. Returns true if
// we now have a usable console (so callers can mirror logs there).
bool TryAttachToParentConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return false;
    }

    // Re-open the standard handles. freopen_s with "CONOUT$" is the
    // canonical recipe; it correctly hooks the C runtime's stdio buffers.
    FILE* f = nullptr;
    if (freopen_s(&f, "CONOUT$", "w", stdout) != 0) {
        // If we attached but failed to bind stdout, nothing useful left to do.
        return false;
    }
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$",  "r", stdin);

    // Disable buffering so log lines appear immediately.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // Print a newline so the prompt that issued the command isn't visually
    // overwritten by our first log line.
    std::fputc('\n', stdout);
    return true;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*pCmdLine*/, int nCmdShow) {
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (!singleInstance) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ActivateExistingInstance();
        CloseHandle(singleInstance);
        return 0;
    }

    // Re-attach to the parent console if we were launched from a terminal.
    bool console = TryAttachToParentConsole();
    wb::Logger::Instance().SetConsoleEnabled(console);

    wb::Logger::Instance().Log(wb::LogLevel::Info,
                               "WASAPI Bridge starting version %s", WB_VERSION);

    int exitCode = wb::RunGui(hInstance, nCmdShow);
    CloseHandle(singleInstance);

    // The GUI window (and the logger's notify target) is already gone here,
    // so this line only reaches the console/terminal -- a clear closing
    // marker before the shell prompt returns.
    wb::Logger::Instance().Log(wb::LogLevel::Info, "WASAPI Bridge exiting");

    if (console) {
        // Newline so the next shell prompt doesn't crowd our last log line.
        std::fputc('\n', stdout);
        FreeConsole();
    }
    return exitCode;
}
