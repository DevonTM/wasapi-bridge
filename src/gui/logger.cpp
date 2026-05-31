#include "logger.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace wb {

namespace {
const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Info:     return "INFO";
        case LogLevel::Warn:     return "WARN";
        case LogLevel::Error:    return "ERROR";
        case LogLevel::Recovery: return "RECOVERY";
        case LogLevel::Notify:   return "NOTIFY";
    }
    return "INFO";
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetConsoleEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleEnabled_ = enabled;
}

void Logger::SetNotifyWindow(HWND hwnd, UINT message) {
    std::lock_guard<std::mutex> lock(mutex_);
    notifyHwnd_    = hwnd;
    notifyMessage_ = message;
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(level, fmt, args);
    va_end(args);
}

void Logger::LogV(LogLevel level, const char* fmt, va_list args) {
    // Render into a stack buffer first; fall back to heap if it overflows.
    char stackBuf[1024];
    va_list argsCopy;
    va_copy(argsCopy, args);
    int needed = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt, argsCopy);
    va_end(argsCopy);

    std::string msg;
    if (needed < 0) {
        msg = "[log format error]";
    } else if (static_cast<size_t>(needed) < sizeof(stackBuf)) {
        msg.assign(stackBuf, static_cast<size_t>(needed));
    } else {
        msg.resize(static_cast<size_t>(needed));
        std::vsnprintf(msg.data(), msg.size() + 1, fmt, args);
    }

    LogLine line{NowMs(), level, std::move(msg)};

    HWND notifyHwnd = nullptr;
    UINT notifyMsg  = 0;
    bool toConsole  = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lines_.push_back(line);
        ++totalAppended_;
        while (lines_.size() > kMaxLines) {
            lines_.pop_front();
        }
        notifyHwnd = notifyHwnd_;
        notifyMsg  = notifyMessage_;
        toConsole  = consoleEnabled_;
    }

    if (toConsole) {
        // stdout flushes line-buffered when attached to a console; stderr
        // for warn/error keeps existing CLI semantics.
        FILE* sink = (level == LogLevel::Warn || level == LogLevel::Error) ? stderr : stdout;
        std::string formatted = Format(line);
        std::fputs(formatted.c_str(), sink);
        std::fputc('\n', sink);
        std::fflush(sink);
    }

    if (notifyHwnd && notifyMsg) {
        // Posted (not sent) so we never block the producer thread on the GUI
        // pump. Multiple posts coalesce naturally because the handler drains
        // every pending line per wake-up.
        PostMessageW(notifyHwnd, notifyMsg, 0, 0);
    }
}

std::vector<LogLine> Logger::Snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    drainCursor_ = totalAppended_;
    return std::vector<LogLine>(lines_.begin(), lines_.end());
}

std::vector<LogLine> Logger::DrainNew() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogLine> out;
    if (drainCursor_ >= totalAppended_) {
        return out;
    }
    uint64_t newCount = totalAppended_ - drainCursor_;
    // If the producer outran us by more than the buffer holds, the older
    // entries already fell out the back. Take whatever's left.
    if (newCount > lines_.size()) {
        newCount = lines_.size();
    }
    out.reserve(static_cast<size_t>(newCount));
    auto it = lines_.end() - static_cast<ptrdiff_t>(newCount);
    out.insert(out.end(), it, lines_.end());
    drainCursor_ = totalAppended_;
    return out;
}

void Logger::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
    drainCursor_ = totalAppended_;
}

std::string Logger::Format(const LogLine& line) {
    // Render local wall-clock time HH:MM:SS so users can correlate with
    // system events (Windows event viewer etc.).
    std::time_t secs = static_cast<std::time_t>(line.timestampMs / 1000);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &secs);
#else
    localtime_r(&secs, &tm);
#endif

    char buf[256];
    std::snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] [%s] %s",
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  LevelTag(line.level),
                  line.message.c_str());
    return std::string(buf);
}

} // namespace wb
