#ifndef WB_TYPES_H
#define WB_TYPES_H

#include <atomic>
#include <mutex>
#include "miniaudio.h"

struct ApplicationData {
    ma_pcm_rb ringBuffer;
    ma_uint32 sourceChannels;
    ma_uint32 targetChannels;
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

#endif // WB_TYPES_H
