#include "bridge_service.h"

#include <chrono>

#include "logger.h"
#include "../callbacks.h"
#include "../device_manager.h"

namespace wb {

const char* BridgeStateLabel(BridgeState state) {
    switch (state) {
        case BridgeState::Stopped:    return "Stopped";
        case BridgeState::Starting:   return "Starting";
        case BridgeState::Running:    return "Running";
        case BridgeState::Recovering: return "Recovering";
        case BridgeState::Stopping:   return "Stopping";
        case BridgeState::Failed:     return "Failed";
    }
    return "Unknown";
}

BridgeService::BridgeService() = default;

BridgeService::~BridgeService() {
    Stop();
}

void BridgeService::SetState(BridgeState s) {
    state_.store(s);
}

std::string BridgeService::LastError() {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

bool BridgeService::Start(const BridgeConfig& config) {
    if (running_.exchange(true)) {
        WB_LOG_WARN("Bridge already running, ignoring Start()");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        lastError_.clear();
    }

    SetState(BridgeState::Starting);

    // A worker that failed during init exits on its own (sets running_=false
    // and returns) without anyone joining it, leaving worker_ joinable.
    // Assigning a new std::thread over a still-joinable one calls
    // std::terminate ("terminate called without an active exception"), so
    // reap the stale thread first. It has already finished, so join() returns
    // immediately. The "already running" guard above ensures we never reach
    // here while a live worker is still running.
    if (worker_.joinable()) {
        worker_.join();
    }

    // Reset shared signal state so a previous Stop() doesn't immediately
    // tell the new worker to exit.
    g_keepRunning.store(true);
    g_recoveryState.needsRecovery.store(false);
    g_recoveryState.isRecovering.store(false);
    g_recoveryState.devicesRunning.store(false);
    g_recoveryState.sourceInitialized.store(false);
    g_recoveryState.targetInitialized.store(false);
    g_recoveryState.ringBufferInitialized.store(false);
    g_recoveryState.lastRecoveryAttemptMs.store(0);

    // Cleared here, set true again by the worker just before it returns (any
    // path). TryGracefulStop() polls this to detect a finished worker without
    // an unbounded join().
    workerExited_.store(false);

    worker_ = std::thread(&BridgeService::WorkerMain, this, config);
    return true;
}

void BridgeService::Stop() {
    bool wasRunning = running_.exchange(false);

    if (wasRunning) {
        SetState(BridgeState::Stopping);

        // Flip the shared run flag and kick the wakeup CV so the worker leaves
        // its recovery-loop wait immediately.
        g_keepRunning.store(false);
        g_wakeupCv.notify_all();
    }

    // Always reap a joinable worker -- not just when we were running. A
    // worker that failed during init self-exits (running_ already false) but
    // leaves worker_ joinable; without this join the destructor would call
    // std::terminate on a still-joinable thread when the app closes in that
    // state. join() on an already-finished thread returns immediately.
    if (worker_.joinable()) {
        worker_.join();
    }

    if (wasRunning) {
        SetState(BridgeState::Stopped);
    }
}

void BridgeService::RequestStop() {
    // Interactive stop: signal the worker and return immediately. Unlike
    // Stop(), we deliberately do NOT join() here -- joining on the GUI thread
    // would freeze the window for the whole WASAPI teardown (long at high
    // latency). The worker tears down on its own and sets state to Stopped via
    // WorkerMain; the GUI's state timer observes the transition. The still-
    // joinable thread is reaped later by Start() (before the next run) or by
    // Stop()/TryGracefulStop()/the destructor on exit.
    bool wasRunning = running_.exchange(false);
    if (wasRunning) {
        SetState(BridgeState::Stopping);
        g_keepRunning.store(false);
        g_wakeupCv.notify_all();
    }
}

bool BridgeService::TryGracefulStop(unsigned timeoutMs) {
    // Signal the worker to stop (same as Stop()), but never block forever on
    // join(). Used only on the exit path so a worker wedged inside a driver-
    // level WASAPI init deadlock can't freeze app shutdown.
    bool wasRunning = running_.exchange(false);
    if (wasRunning) {
        SetState(BridgeState::Stopping);
        g_keepRunning.store(false);
        g_wakeupCv.notify_all();
    }

    if (!worker_.joinable()) {
        return true;  // nothing to wait on
    }

    // Poll workerExited_ (set by WorkerMain's RAII guard on every return path)
    // up to the timeout. join() only AFTER we've confirmed the thread really
    // finished, so the join itself returns immediately and never blocks.
    const unsigned step = 10;
    for (unsigned waited = 0; waited < timeoutMs; waited += step) {
        if (workerExited_.load()) {
            worker_.join();
            if (wasRunning) SetState(BridgeState::Stopped);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
    }

    // Timed out: the worker is wedged (almost certainly inside a synchronous
    // WASAPI init). Do NOT join -- that would hang. Report failure so the
    // caller force-terminates the process; the OS reclaims the stuck thread.
    return workerExited_.load() ? (worker_.join(), true) : false;
}

namespace {
#ifndef NDEBUG
constexpr bool kEnableRingStatsLog = true;
#else
constexpr bool kEnableRingStatsLog = false;
#endif

// RAII guard: sets an atomic<bool> true when it goes out of scope. Used so
// WorkerMain marks workerExited_ on *every* return path (context-init fail,
// bridge-init fail, or normal shutdown) without repeating the store.
struct ScopedFlagOnExit {
    std::atomic<bool>& flag;
    ~ScopedFlagOnExit() { flag.store(true); }
};
} // namespace

void BridgeService::WorkerMain(BridgeConfig config) {
    // Mark the worker as exited on any return path (see TryGracefulStop).
    ScopedFlagOnExit exitGuard{workerExited_};

    // Each worker run owns its own miniaudio context. Tearing it down on
    // exit means re-Start() picks up fresh device topology, which matters
    // when users plug/unplug devices between runs.
    ma_context context{};
    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.threadPriority = ma_thread_priority_realtime;

    ma_backend backend = ma_backend_wasapi;
    if (ma_result rc = ma_context_init(&backend, 1, &contextConfig, &context); rc != MA_SUCCESS) {
        WB_LOG_ERROR("Failed to initialize WASAPI context: %s (%d)",
                     ma_result_description(rc), static_cast<int>(rc));
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            lastError_ = "Failed to initialize WASAPI context";
        }
        SetState(BridgeState::Failed);
        running_.store(false);
        return;
    }

    ma_device sourceDevice{};
    ma_device targetDevice{};
    ApplicationData appData{};

    WB_LOG_INFO("Initializing bridge...");
    if (!initialize_bridge(&context, config, &sourceDevice, &targetDevice, &appData)) {
        WB_LOG_ERROR("Failed to initialize bridge");
        {
            std::lock_guard<std::mutex> lock(errorMutex_);
            lastError_ = "Failed to initialize bridge (see log for details)";
        }
        ma_context_uninit(&context);
        SetState(BridgeState::Failed);
        running_.store(false);
        return;
    }

    g_recoveryState.devicesRunning.store(true);
    SetState(BridgeState::Running);

    WB_LOG_INFO("Bridge running. Automatic recovery enabled.");
#ifndef NDEBUG
    auto lastStatsLog = std::chrono::steady_clock::now();
    ma_uint64 lastOverflows = 0;
    ma_uint64 lastUnderflows = 0;
    ma_uint64 lastInactiveUnderflows = 0;
#endif

    // Recovery loop. The 100 ms timeout keeps recovery debouncing responsive
    // without busy-spinning.
    while (g_keepRunning.load()) {
        if (g_recoveryState.needsRecovery.load() && !g_recoveryState.isRecovering.load()) {
            int64_t now = get_current_time_ms();
            int64_t since = now - g_recoveryState.lastRecoveryAttemptMs.load();
            if (since >= 3000) {
                SetState(BridgeState::Recovering);
                bool ok = attempt_recovery(&context, config, &sourceDevice, &targetDevice, &appData);
                SetState(ok ? BridgeState::Running : BridgeState::Recovering);
            }
        }

#ifndef NDEBUG
        if constexpr (kEnableRingStatsLog) {
            auto nowStats = std::chrono::steady_clock::now();
            if (nowStats - lastStatsLog >= std::chrono::seconds(1)) {
                lastStatsLog = nowStats;
                ma_uint64 overflows = appData.overflowFrames.load();
                ma_uint64 underflows = appData.underflowFrames.load();
                ma_uint64 inactiveUnderflows = appData.inactiveUnderflowFrames.load();
                ma_uint32 minFill = appData.minFillFrames.exchange(UINT32_MAX);
                ma_uint32 maxFill = appData.maxFillFrames.exchange(0);
                if (minFill == UINT32_MAX) minFill = appData.lastFillFrames.load();

                WB_LOG_INFO("Ring stats: %s fill=%u frames, min=%u, max=%u, prefill=%u, overflow +%llu total=%llu, active underflow +%llu total=%llu, inactive +%llu total=%llu, callbacks C/P=%llu/%llu",
                            appData.streamActive.load() ? "active" : "prefill",
                            appData.lastFillFrames.load(),
                            minFill,
                            maxFill,
                            appData.prefillFrames.load(),
                            static_cast<unsigned long long>(overflows - lastOverflows),
                            static_cast<unsigned long long>(overflows),
                            static_cast<unsigned long long>(underflows - lastUnderflows),
                            static_cast<unsigned long long>(underflows),
                            static_cast<unsigned long long>(inactiveUnderflows - lastInactiveUnderflows),
                            static_cast<unsigned long long>(inactiveUnderflows),
                            static_cast<unsigned long long>(appData.captureCallbacks.load()),
                            static_cast<unsigned long long>(appData.playbackCallbacks.load()));
                lastOverflows = overflows;
                lastUnderflows = underflows;
                lastInactiveUnderflows = inactiveUnderflows;
            }
        }
#endif

        std::unique_lock<std::mutex> lock(g_wakeupMutex);
        g_wakeupCv.wait_for(lock, std::chrono::milliseconds(100), [] {
            return !g_keepRunning.load() ||
                   (g_recoveryState.needsRecovery.load() && !g_recoveryState.isRecovering.load());
        });
    }

    WB_LOG_INFO("Shutting down bridge...");
    g_recoveryState.devicesRunning.store(false);
    bool srcInit = g_recoveryState.sourceInitialized.load();
    bool tgtInit = g_recoveryState.targetInitialized.load();
    bool rbInit  = g_recoveryState.ringBufferInitialized.load();
    cleanup_devices(&sourceDevice, &targetDevice, &appData, &srcInit, &tgtInit, &rbInit);
    g_recoveryState.sourceInitialized.store(srcInit);
    g_recoveryState.targetInitialized.store(tgtInit);
    g_recoveryState.ringBufferInitialized.store(rbInit);
    ma_context_uninit(&context);

    WB_LOG_INFO("Bridge stopped.");
    SetState(BridgeState::Stopped);
}

} // namespace wb
