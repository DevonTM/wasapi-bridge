#ifndef WB_GUI_BRIDGE_SERVICE_H
#define WB_GUI_BRIDGE_SERVICE_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "../types.h"
#include "miniaudio.h"

namespace wb {

// Lifecycle of the audio bridge as observed by the GUI. The state machine
// is purposely small; recovery internals live in device_manager.cpp.
enum class BridgeState {
    Stopped,
    Starting,
    Running,
    Recovering,
    Stopping,
    Failed,
};

const char* BridgeStateLabel(BridgeState state);

// Owns the worker thread that runs the original main-loop logic from
// src/main.cpp. The GUI calls Start()/Stop() and polls State()/IsRunning().
//
// One-instance contract: only a single bridge can run at a time (the global
// recovery state in src/types.h is shared). Don't try to spin up two.
class BridgeService {
public:
    BridgeService();
    ~BridgeService();

    BridgeService(const BridgeService&) = delete;
    BridgeService& operator=(const BridgeService&) = delete;

    // Returns true if the worker started. Returns false (and logs) if the
    // service is already running, the context cannot be initialised, or the
    // initial bridge setup fails. The worker continues running until Stop()
    // is called even if recovery cycles fail repeatedly.
    bool Start(const BridgeConfig& config);

    // Signals the worker to exit and joins it. Idempotent; calling Stop()
    // on a stopped service is a no-op.
    void Stop();

    // Non-blocking stop for the interactive (button) path: signals the worker
    // to stop but does NOT join, so the GUI thread stays responsive during the
    // (potentially long) WASAPI teardown. The worker self-exits and sets state
    // to Stopped; the GUI's state timer observes that. The still-joinable
    // thread is reaped later by Start() (before a new run) or by Stop()/
    // TryGracefulStop() on exit.
    void RequestStop();

    // Exit-path variant of Stop(): signals the worker, then waits up to
    // timeoutMs for it to actually finish before joining. Returns true if the
    // worker stopped (or none was running). Returns false if it did NOT stop
    // in time -- e.g. wedged inside a driver-level WASAPI init deadlock -- in
    // which case the caller must NOT join (that would freeze) and should
    // force the process to terminate instead. Unlike Stop(), this never
    // blocks indefinitely.
    bool TryGracefulStop(unsigned timeoutMs);

    bool IsRunning() const { return running_.load(); }

    BridgeState State() const { return state_.load(); }

    // Last fatal error message (set when state transitions to Failed).
    std::string LastError();

private:
    void WorkerMain(BridgeConfig config);
    void SetState(BridgeState state);

    std::atomic<bool>        running_{false};
    // Set true by the worker just before WorkerMain returns (any path), reset
    // to false in Start(). Lets TryGracefulStop() detect a finished worker
    // without an unbounded join().
    std::atomic<bool>        workerExited_{true};
    std::atomic<BridgeState> state_{BridgeState::Stopped};
    std::thread              worker_;
    std::mutex               errorMutex_;
    std::string              lastError_;
};

} // namespace wb

#endif // WB_GUI_BRIDGE_SERVICE_H
