#include "device_manager.h"
#include "callbacks.h"
#include "gui/logger.h"
#include <thread>
#include <chrono>

int64_t get_current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void cleanup_devices(ma_device* sourceDevice, ma_device* targetDevice,
                     ApplicationData* appData, bool* sourceInitialized,
                     bool* targetInitialized, bool* ringBufferInitialized) {
    // Stop devices if they're running
    if (targetDevice && *targetInitialized && ma_device_is_started(targetDevice)) {
        ma_device_stop(targetDevice);
    }
    if (sourceDevice && *sourceInitialized && ma_device_is_started(sourceDevice)) {
        ma_device_stop(sourceDevice);
    }

    // Small delay to ensure callbacks have finished
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Only uninit if device was actually initialized
    if (sourceDevice && *sourceInitialized) {
        ma_device_uninit(sourceDevice);
        *sourceInitialized = false;
    }
    if (targetDevice && *targetInitialized) {
        ma_device_uninit(targetDevice);
        *targetInitialized = false;
    }
    if (appData && *ringBufferInitialized) {
        ma_pcm_rb_uninit(&appData->ringBuffer);
        *ringBufferInitialized = false;
    }
}

bool initialize_bridge(ma_context* context, const BridgeConfig& config,
                       ma_device* sourceDevice, ma_device* targetDevice,
                       ApplicationData* appData) {
    // Reset application data
    appData->sourceChannels.store(2);
    appData->targetChannels.store(2);

    // Initialize source device (loopback)
    ma_device_config sourceConfig = ma_device_config_init(ma_device_type_loopback);
    sourceConfig.capture.pDeviceID = &config.sourceDeviceId;
    sourceConfig.capture.format = ma_format_f32;
    sourceConfig.dataCallback = capture_callback;
    sourceConfig.notificationCallback = device_notification_callback;
    sourceConfig.pUserData = appData;
    sourceConfig.performanceProfile = ma_performance_profile_low_latency;
    sourceConfig.wasapi.usage = ma_wasapi_usage_pro_audio;

    {
        ma_result result = ma_device_init(context, &sourceConfig, sourceDevice);
        if (result != MA_SUCCESS) {
            WB_LOG_ERROR("Failed to initialize source loopback device: %s (%d)",
                         ma_result_description(result), static_cast<int>(result));
            return false;
        }
    }

    appData->sourceChannels.store(sourceDevice->capture.channels);
    WB_LOG_INFO("Source device initialized at %u Hz, %u channels",
                sourceDevice->capture.internalSampleRate,
                appData->sourceChannels.load());

    // Initialize target device (playback)
    ma_device_config targetConfig = ma_device_config_init(ma_device_type_playback);
    targetConfig.playback.pDeviceID = &config.targetDeviceId;
    targetConfig.playback.format = ma_format_f32;
    targetConfig.sampleRate = sourceDevice->sampleRate;
    targetConfig.playback.shareMode = config.shareMode;
    targetConfig.periodSizeInMilliseconds = config.targetLatency;
    targetConfig.dataCallback = playback_callback;
    targetConfig.notificationCallback = device_notification_callback;
    targetConfig.pUserData = appData;
    targetConfig.performanceProfile = ma_performance_profile_low_latency;
    targetConfig.wasapi.usage = ma_wasapi_usage_pro_audio;

    {
        ma_result result = ma_device_init(context, &targetConfig, targetDevice);
        if (result != MA_SUCCESS) {
            WB_LOG_ERROR("Failed to initialize target device: %s (%d)",
                         ma_result_description(result), static_cast<int>(result));
            ma_device_uninit(sourceDevice);
            return false;
        }
    }

    appData->targetChannels.store(targetDevice->playback.channels);
    WB_LOG_INFO("Target device initialized at %u Hz, %u channels",
                targetDevice->playback.internalSampleRate,
                appData->targetChannels.load());

    // Size the ring buffer based on actual period sizes chosen by miniaudio.
    // Use a single sub-buffer: ma_rb_acquire_read returns "available" bounded
    // by the current sub-buffer when the loop flags differ, which causes
    // partial reads on every sub-buffer boundary in a multi-sub-buffer ring.
    // Single sub-buffer means there's only one wraparound per ring, and the
    // wraparound loop in callbacks.cpp handles it cleanly.
    ma_uint32 srcPeriod = sourceDevice->capture.internalPeriodSizeInFrames;
    ma_uint32 tgtPeriod = targetDevice->playback.internalPeriodSizeInFrames;
    ma_uint32 maxPeriod = (srcPeriod > tgtPeriod) ? srcPeriod : tgtPeriod;
    if (maxPeriod == 0) {
        // Defensive fallback - WASAPI always populates these, but just in case.
        maxPeriod = sourceDevice->sampleRate / 100; // ~10 ms
    }

    ma_uint32 totalFrames = maxPeriod * 4;

    WB_LOG_INFO("Ring buffer: %u frames (~%u ms)",
                totalFrames,
                totalFrames * 1000 / sourceDevice->sampleRate);

    // Initialize ring buffer
    {
        ma_result result = ma_pcm_rb_init(ma_format_f32, appData->targetChannels.load(),
                                          totalFrames, NULL, NULL, &appData->ringBuffer);
        if (result != MA_SUCCESS) {
            WB_LOG_ERROR("Failed to initialize ring buffer: %s (%d)",
                         ma_result_description(result), static_cast<int>(result));
            ma_device_uninit(targetDevice);
            ma_device_uninit(sourceDevice);
            return false;
        }
    }

    // Mark devices as initialized
    g_recoveryState.sourceInitialized = true;
    g_recoveryState.targetInitialized = true;
    g_recoveryState.ringBufferInitialized = true;

    // Start devices
    {
        ma_result result = ma_device_start(sourceDevice);
        if (result != MA_SUCCESS) {
            WB_LOG_ERROR("Failed to start source device: %s (%d)",
                         ma_result_description(result), static_cast<int>(result));
            bool srcInit = g_recoveryState.sourceInitialized;
            bool tgtInit = g_recoveryState.targetInitialized;
            bool rbInit = g_recoveryState.ringBufferInitialized;
            cleanup_devices(sourceDevice, targetDevice, appData, &srcInit, &tgtInit, &rbInit);
            g_recoveryState.sourceInitialized = srcInit;
            g_recoveryState.targetInitialized = tgtInit;
            g_recoveryState.ringBufferInitialized = rbInit;
            return false;
        }
    }

    {
        ma_result result = ma_device_start(targetDevice);
        if (result != MA_SUCCESS) {
            WB_LOG_ERROR("Failed to start target device: %s (%d)",
                         ma_result_description(result), static_cast<int>(result));
            bool srcInit = g_recoveryState.sourceInitialized;
            bool tgtInit = g_recoveryState.targetInitialized;
            bool rbInit = g_recoveryState.ringBufferInitialized;
            cleanup_devices(sourceDevice, targetDevice, appData, &srcInit, &tgtInit, &rbInit);
            g_recoveryState.sourceInitialized = srcInit;
            g_recoveryState.targetInitialized = tgtInit;
            g_recoveryState.ringBufferInitialized = rbInit;
            return false;
        }
    }

    return true;
}

bool attempt_recovery(ma_context* context, const BridgeConfig& config,
                      ma_device* sourceDevice, ma_device* targetDevice,
                      ApplicationData* appData) {
    std::lock_guard<std::mutex> lock(g_recoveryState.recoveryMutex);

    if (g_recoveryState.isRecovering) {
        return false; // Already recovering
    }

    g_recoveryState.isRecovering = true;
    g_recoveryState.needsRecovery = false;
    g_recoveryState.devicesRunning = false;

    WB_LOG_RECOVERY("Attempting to recover bridge...");
    WB_LOG_RECOVERY("Cleaning up devices...");

    // Cleanup existing devices (only if they were initialized)
    bool srcInit = g_recoveryState.sourceInitialized;
    bool tgtInit = g_recoveryState.targetInitialized;
    bool rbInit = g_recoveryState.ringBufferInitialized;
    cleanup_devices(sourceDevice, targetDevice, appData, &srcInit, &tgtInit, &rbInit);
    g_recoveryState.sourceInitialized = srcInit;
    g_recoveryState.targetInitialized = tgtInit;
    g_recoveryState.ringBufferInitialized = rbInit;

    // Wait for system to stabilize
    for (int i = 0; i < 10 && g_keepRunning; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!g_keepRunning) {
        g_recoveryState.isRecovering = false;
        return false;
    }

    g_recoveryState.lastRecoveryAttemptMs = get_current_time_ms();

    WB_LOG_RECOVERY("Reinitializing bridge...");

    // Attempt to reinitialize
    bool success = initialize_bridge(context, config, sourceDevice, targetDevice, appData);

    if (success) {
        WB_LOG_RECOVERY("Bridge successfully recovered!");
        WB_LOG_RECOVERY("Streaming resumed...");
        // Mark devices as running and clear recovery flag
        g_recoveryState.devicesRunning = true;
        g_recoveryState.needsRecovery = false;
    } else {
        WB_LOG_ERROR("Failed to recover bridge. Will retry in 3 seconds...");
        // Set flag to try again
        g_recoveryState.needsRecovery = true;
        g_recoveryState.devicesRunning = false;
    }

    g_recoveryState.isRecovering = false;
    return success;
}
