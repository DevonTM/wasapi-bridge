#ifndef WB_TYPES_H
#define WB_TYPES_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include "miniaudio.h"

struct ApplicationData {
    ma_pcm_rb ringBuffer;
    std::atomic<ma_uint32> sourceChannels;
    std::atomic<ma_uint32> targetChannels;
#ifndef NDEBUG
    std::atomic<ma_uint64> captureCallbacks;
    std::atomic<ma_uint64> playbackCallbacks;
    std::atomic<ma_uint64> overflowFrames;
    std::atomic<ma_uint64> underflowFrames;
    std::atomic<ma_uint64> inactiveUnderflowFrames;
    std::atomic<ma_uint32> lastFillFrames;
    std::atomic<ma_uint32> minFillFrames;
    std::atomic<ma_uint32> maxFillFrames;
#endif
    std::atomic<ma_uint32> prefillFrames;
    std::atomic<bool> streamActive;
};

struct BridgeConfig {
    ma_device_id sourceDeviceId;
    ma_device_id targetDeviceId;
    ma_share_mode shareMode;
    ma_uint32 targetLatency;
};

struct RecoveryState {
    std::atomic<bool> needsRecovery{false};
    std::atomic<bool> isRecovering{false};
    std::atomic<int64_t> lastRecoveryAttemptMs{0};
    std::atomic<bool> devicesRunning{false};
    std::atomic<bool> sourceInitialized{false};
    std::atomic<bool> targetInitialized{false};
    std::atomic<bool> ringBufferInitialized{false};
    std::mutex recoveryMutex;
};

// Global state
extern std::atomic<bool> g_keepRunning;
extern RecoveryState g_recoveryState;

// Wakeup primitive used by the bridge worker. Notified from device events and
// stop requests so the worker reacts immediately instead of waiting for its
// periodic poll timeout to expire.
extern std::mutex g_wakeupMutex;
extern std::condition_variable g_wakeupCv;

#endif // WB_TYPES_H
