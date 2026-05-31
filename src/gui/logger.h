#ifndef WB_GUI_LOGGER_H
#define WB_GUI_LOGGER_H

#include <cstdarg>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>

namespace wb {

// Severity levels mirror the prefixes the original CLI used so the on-screen
// log matches what users have seen historically. Stored in the line so the
// GUI can colour-code or filter later if we want to.
enum class LogLevel {
    Info,
    Warn,
    Error,
    Recovery,
    Notify,
};

struct LogLine {
    int64_t       timestampMs;  // Wall clock, ms since epoch
    LogLevel      level;
    std::string   message;      // Already formatted, no trailing newline
};

// Thread-safe ring buffer that doubles as the central log facility.
// - The bridge worker, callbacks, and main thread all push lines through Log().
// - The GUI reads via Snapshot() / DrainNew() to render the Log tab.
// - When attached to a parent console (Cline/PowerShell launch), each line is
//   also written to stdout so terminal users still see live output.
class Logger {
public:
    static Logger& Instance();

    // Hard cap; ~5000 lines * ~120 bytes ≈ 600 KB worst case.
    static constexpr size_t kMaxLines = 5000;

    void SetConsoleEnabled(bool enabled);

    // Optional: set a window that receives WM_APP+1 whenever a new line lands.
    // The GUI uses this to coalesce paint updates instead of repainting on
    // every push.
    void SetNotifyWindow(HWND hwnd, UINT message);

    // Push a formatted line. Safe to call from any thread, including audio
    // callbacks (allocations are minimised but not zero -- we accept that
    // logging from the realtime callback is rare).
    void Log(LogLevel level, const char* fmt, ...);
    void LogV(LogLevel level, const char* fmt, va_list args);

    // Returns a copy of all retained lines. Used on Log tab first paint.
    std::vector<LogLine> Snapshot();

    // Returns lines added since the last call to DrainNew() and updates the
    // internal cursor. Used by the GUI's WM_APP handler so the Log edit
    // control only appends what is genuinely new.
    std::vector<LogLine> DrainNew();

    void Clear();

    // Format a line for display: "[HH:MM:SS] [LEVEL] message".
    static std::string Format(const LogLine& line);

private:
    Logger() = default;

    std::mutex             mutex_;
    std::deque<LogLine>    lines_;
    uint64_t               totalAppended_ = 0;
    uint64_t               drainCursor_   = 0;
    bool                   consoleEnabled_ = false;
    HWND                   notifyHwnd_     = nullptr;
    UINT                   notifyMessage_  = 0;
};

// Convenience macros so call sites stay terse. They expand to printf-like
// calls into the singleton.
#define WB_LOG_INFO(...)     ::wb::Logger::Instance().Log(::wb::LogLevel::Info,     __VA_ARGS__)
#define WB_LOG_WARN(...)     ::wb::Logger::Instance().Log(::wb::LogLevel::Warn,     __VA_ARGS__)
#define WB_LOG_ERROR(...)    ::wb::Logger::Instance().Log(::wb::LogLevel::Error,    __VA_ARGS__)
#define WB_LOG_RECOVERY(...) ::wb::Logger::Instance().Log(::wb::LogLevel::Recovery, __VA_ARGS__)
#define WB_LOG_NOTIFY(...)   ::wb::Logger::Instance().Log(::wb::LogLevel::Notify,   __VA_ARGS__)

} // namespace wb

#endif // WB_GUI_LOGGER_H
