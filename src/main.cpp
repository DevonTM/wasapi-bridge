#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <windows.h>

#include "miniaudio.h"

#define WB_VERSION "0.1.4"

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

std::atomic<bool> g_keepRunning{true};
RecoveryState g_recoveryState;

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        g_keepRunning = false;
        return TRUE;
    }
    return FALSE;
}

int64_t get_current_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

void device_notification_callback(const ma_device_notification* pNotification) {
    if (!pNotification || !pNotification->pDevice) {
        return;
    }

    // Ignore notifications during recovery to prevent infinite loops
    if (g_recoveryState.isRecovering) {
        return;
    }

    // Only process notifications when devices are supposed to be running
    if (!g_recoveryState.devicesRunning) {
        return;
    }

    const char* deviceType = (pNotification->pDevice->type == ma_device_type_loopback) ? "SOURCE" : "TARGET";

    switch (pNotification->type) {
        case ma_device_notification_type_stopped:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device stopped unexpectedly\n";
            g_recoveryState.needsRecovery = true;
            g_recoveryState.devicesRunning = false;
            break;

        case ma_device_notification_type_rerouted:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device rerouted\n";
            g_recoveryState.needsRecovery = true;
            g_recoveryState.devicesRunning = false;
            break;

        case ma_device_notification_type_interruption_began:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device interruption began\n";
            g_recoveryState.needsRecovery = true;
            g_recoveryState.devicesRunning = false;
            break;

        case ma_device_notification_type_interruption_ended:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device interruption ended\n";
            g_recoveryState.needsRecovery = true;
            break;

        case ma_device_notification_type_started:
            // Device started successfully, no action needed
            break;

        case ma_device_notification_type_unlocked:
            // Web audio context unlocked, not relevant for WASAPI
            break;

        default:
            break;
    }
}

void cleanup_devices(ma_device* sourceDevice, ma_device* targetDevice, ApplicationData* appData, bool* sourceInitialized, bool* targetInitialized, bool* ringBufferInitialized) {
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

void capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    ApplicationData* appData = (ApplicationData*)pDevice->pUserData;
    if (pInput == nullptr || appData == nullptr) return;

    ma_uint32 framesToWrite = frameCount;
    ma_uint32 framesAvailable = ma_pcm_rb_available_write(&appData->ringBuffer);
    if (framesToWrite > framesAvailable) {
        framesToWrite = framesAvailable;
    }

    if (framesToWrite > 0) {
        void* pWriteBuffer;
        ma_pcm_rb_acquire_write(&appData->ringBuffer, &framesToWrite, &pWriteBuffer);

        // Copy logic for source -> target mapping
        float* pSrc = (float*)pInput;
        float* pDst = (float*)pWriteBuffer;

        for (ma_uint32 i = 0; i < framesToWrite; ++i) {
            for (ma_uint32 c = 0; c < appData->targetChannels; ++c) {
                if (c < appData->sourceChannels) {
                    float sample = pSrc[i * appData->sourceChannels + c];
                    // Handle peak clipping correctly (Hard clamp to valid float audio range [-1.0, 1.0])
                    pDst[i * appData->targetChannels + c] = std::clamp(sample, -1.0f, 1.0f);
                } else {
                    pDst[i * appData->targetChannels + c] = 0.0f;
                }
            }
        }

        ma_pcm_rb_commit_write(&appData->ringBuffer, framesToWrite);
    }
}

void playback_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    ApplicationData* appData = (ApplicationData*)pDevice->pUserData;
    if (pOutput == nullptr || appData == nullptr) return;

    ma_uint32 framesToRead = frameCount;
    ma_uint32 framesAvailable = ma_pcm_rb_available_read(&appData->ringBuffer);

    if (framesToRead > framesAvailable) {
        // Not enough data. Just read what's available and zero the rest if we wanted.
        framesToRead = framesAvailable;
    }

    float* pDst = (float*)pOutput;

    if (framesToRead > 0) {
        void* pReadBuffer;
        ma_pcm_rb_acquire_read(&appData->ringBuffer, &framesToRead, &pReadBuffer);

        float* pSrc = (float*)pReadBuffer;
        for (ma_uint32 i = 0; i < framesToRead * appData->targetChannels; ++i) {
            pDst[i] = pSrc[i];
        }

        ma_pcm_rb_commit_read(&appData->ringBuffer, framesToRead);
    }

    // Fill remainder with zeroes stringently (handle underflow)
    if (framesToRead < frameCount) {
        ma_uint32 zeroes = (frameCount - framesToRead) * appData->targetChannels;
        for(ma_uint32 i = 0; i < zeroes; i++) {
            pDst[framesToRead * appData->targetChannels + i] = 0.0f;
        }
    }
}

bool initialize_bridge(ma_context* context, const BridgeConfig& config,
                       ma_device* sourceDevice, ma_device* targetDevice,
                       ApplicationData* appData) {
    // Reset application data
    appData->sourceChannels = 2;
    appData->targetChannels = 2;

    // Initialize source device (loopback)
    ma_device_config sourceConfig = ma_device_config_init(ma_device_type_loopback);
    sourceConfig.capture.pDeviceID = &config.sourceDeviceId;
    sourceConfig.capture.format = ma_format_f32;
    sourceConfig.dataCallback = capture_callback;
    sourceConfig.notificationCallback = device_notification_callback;
    sourceConfig.pUserData = appData;
    sourceConfig.performanceProfile = ma_performance_profile_low_latency;
    sourceConfig.wasapi.usage = ma_wasapi_usage_pro_audio;

    if (ma_device_init(context, &sourceConfig, sourceDevice) != MA_SUCCESS) {
        std::cerr << "[ERROR] Failed to initialize source loopback device\n";
        return false;
    }

    appData->sourceChannels = sourceDevice->capture.channels;
    std::cout << "[INFO] Source device initialized at " << sourceDevice->sampleRate << " Hz, "
              << appData->sourceChannels << " channels\n";

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

    if (ma_device_init(context, &targetConfig, targetDevice) != MA_SUCCESS) {
        std::cerr << "[ERROR] Failed to initialize target device\n";
        ma_device_uninit(sourceDevice);
        return false;
    }

    appData->targetChannels = targetDevice->playback.channels;
    std::cout << "[INFO] Target device initialized at " << targetDevice->sampleRate << " Hz, "
              << appData->targetChannels << " channels\n";

    // Initialize ring buffer
    ma_result result = ma_pcm_rb_init(ma_format_f32, appData->targetChannels,
                                       sourceDevice->sampleRate * 2, NULL, NULL,
                                       &appData->ringBuffer);
    if (result != MA_SUCCESS) {
        std::cerr << "[ERROR] Failed to initialize ring buffer\n";
        ma_device_uninit(targetDevice);
        ma_device_uninit(sourceDevice);
        return false;
    }

    // Mark devices as initialized
    g_recoveryState.sourceInitialized = true;
    g_recoveryState.targetInitialized = true;
    g_recoveryState.ringBufferInitialized = true;

    // Start devices
    if (ma_device_start(sourceDevice) != MA_SUCCESS) {
        std::cerr << "[ERROR] Failed to start source device\n";
        bool srcInit = g_recoveryState.sourceInitialized;
        bool tgtInit = g_recoveryState.targetInitialized;
        bool rbInit = g_recoveryState.ringBufferInitialized;
        cleanup_devices(sourceDevice, targetDevice, appData, &srcInit, &tgtInit, &rbInit);
        g_recoveryState.sourceInitialized = srcInit;
        g_recoveryState.targetInitialized = tgtInit;
        g_recoveryState.ringBufferInitialized = rbInit;
        return false;
    }

    if (ma_device_start(targetDevice) != MA_SUCCESS) {
        std::cerr << "[ERROR] Failed to start target device\n";
        bool srcInit = g_recoveryState.sourceInitialized;
        bool tgtInit = g_recoveryState.targetInitialized;
        bool rbInit = g_recoveryState.ringBufferInitialized;
        cleanup_devices(sourceDevice, targetDevice, appData, &srcInit, &tgtInit, &rbInit);
        g_recoveryState.sourceInitialized = srcInit;
        g_recoveryState.targetInitialized = tgtInit;
        g_recoveryState.ringBufferInitialized = rbInit;
        return false;
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
    g_recoveryState.lastRecoveryAttemptMs = get_current_time_ms();

    std::cout << "\n[RECOVERY] Attempting to recover bridge...\n";
    std::cout << "[RECOVERY] Cleaning up devices...\n";

    // Cleanup existing devices (only if they were initialized)
    bool srcInit = g_recoveryState.sourceInitialized;
    bool tgtInit = g_recoveryState.targetInitialized;
    bool rbInit = g_recoveryState.ringBufferInitialized;
    cleanup_devices(sourceDevice, targetDevice, appData, &srcInit, &tgtInit, &rbInit);
    g_recoveryState.sourceInitialized = srcInit;
    g_recoveryState.targetInitialized = tgtInit;
    g_recoveryState.ringBufferInitialized = rbInit;

    // Wait 2 seconds before attempting recovery (debounce)
    std::cout << "[RECOVERY] Waiting 2 seconds before reinitializing...\n";
    for (int i = 0; i < 20 && g_keepRunning; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!g_keepRunning) {
        g_recoveryState.isRecovering = false;
        return false;
    }

    std::cout << "[RECOVERY] Reinitializing bridge...\n";

    // Attempt to reinitialize
    bool success = initialize_bridge(context, config, sourceDevice, targetDevice, appData);

    if (success) {
        std::cout << "[RECOVERY] Bridge successfully recovered!\n";
        std::cout << "[RECOVERY] Streaming resumed...\n";
        // Mark devices as running and clear recovery flag
        g_recoveryState.devicesRunning = true;
        g_recoveryState.needsRecovery = false;
    } else {
        std::cerr << "[RECOVERY] Failed to recover bridge. Will retry in 2 seconds...\n";
        // Set flag to try again
        g_recoveryState.needsRecovery = true;
        g_recoveryState.devicesRunning = false;
    }

    g_recoveryState.isRecovering = false;
    return success;
}

int main(int argc, char** argv) {
    SetConsoleTitleA("WASAPI Bridge");
    std::cout << "Starting WASAPI Bridge v" << WB_VERSION << "\n";

    ma_context context;
    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.threadPriority = ma_thread_priority_realtime;

    ma_backend backend = ma_backend_wasapi;
    if (ma_context_init(&backend, 1, &contextConfig, &context) != MA_SUCCESS) {
        std::cerr << "Failed to initialize standard WASAPI context.\n";
        return -1;
    }

    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;
    if (ma_context_get_devices(&context, &pPlaybackInfos, &playbackCount, NULL, NULL) != MA_SUCCESS) {
        std::cerr << "Failed to enumerate devices.\n";
        ma_context_uninit(&context);
        return -1;
    }

    std::cout << "\nAvailable Playback Devices:\n";
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        std::cout << i + 1 << ". " << pPlaybackInfos[i].name << "\n";
    }

    int sourceChoice = 0;
    int targetChoice = 0;

    while (true) {
        while (true) {
            std::cout << "\nSelect SOURCE audio device (Loopback): ";
            if (std::cin >> sourceChoice && sourceChoice >= 1 && static_cast<ma_uint32>(sourceChoice) <= playbackCount) break;
            std::cout << "Invalid choice. Please enter a valid number.\n";
            std::cin.clear();
            std::cin.ignore(10000, '\n');
        }

        while (true) {
            std::cout << "Select TARGET audio device (Output): ";
            if (std::cin >> targetChoice && targetChoice >= 1 && static_cast<ma_uint32>(targetChoice) <= playbackCount) break;
            std::cout << "Invalid choice. Please enter a valid number.\n";
            std::cin.clear();
            std::cin.ignore(10000, '\n');
        }

        if (sourceChoice != targetChoice) {
            break;
        }
        std::cout << "SOURCE and TARGET cannot be the same device. Please select again.\n";
    }

    std::cout << "\nWASAPI Modes:\n1. Shared\n2. Exclusive\n";
    int modeChoice = 0;
    while (true) {
        std::cout << "Select Mode for TARGET device: ";
        if (std::cin >> modeChoice && (modeChoice == 1 || modeChoice == 2)) break;
        std::cout << "Invalid choice. Please enter 1 or 2.\n";
        std::cin.clear();
        std::cin.ignore(10000, '\n');
    }
    ma_share_mode shareMode = (modeChoice == 2) ? ma_share_mode_exclusive : ma_share_mode_shared;

    uint32_t defaultLatency = (shareMode == ma_share_mode_exclusive) ? 5 : 10;
    uint32_t targetLatency = defaultLatency;

    while (true) {
        std::cout << "\nEnter desired target latency in milliseconds (default " << defaultLatency << "): ";
        std::string latencyInput;
        std::cin.ignore();
        std::getline(std::cin, latencyInput);

        if (latencyInput.empty()) {
            targetLatency = defaultLatency;
            std::cout << "Using default latency " << targetLatency << " ms\n";
            break;
        }

        try {
            int latencyValue = std::stoi(latencyInput);
            if (latencyValue < 0) {
                std::cout << "Latency cannot be negative. Please enter a valid positive number.\n";
                continue;
            }
            if (latencyValue == 0) {
                targetLatency = defaultLatency;
                std::cout << "Using default latency " << targetLatency << " ms\n";
                break;
            }
            targetLatency = static_cast<uint32_t>(latencyValue);
            std::cout << "Latency set to " << targetLatency << " ms\n";
            break;
        } catch(...) {
            std::cout << "Invalid input. Please enter a valid positive number.\n";
        }
    }

    // Store configuration for recovery
    BridgeConfig config;
    config.sourceDeviceId = pPlaybackInfos[sourceChoice - 1].id;
    config.targetDeviceId = pPlaybackInfos[targetChoice - 1].id;
    config.shareMode = shareMode;
    config.targetLatency = targetLatency;

    ma_device sourceDevice;
    ma_device targetDevice;
    ApplicationData appData = {};

    std::cout << "\n[INFO] Initializing bridge...\n";
    if (!initialize_bridge(&context, config, &sourceDevice, &targetDevice, &appData)) {
        std::cerr << "[ERROR] Failed to initialize bridge\n";
        ma_context_uninit(&context);
        return -1;
    }

    // Mark devices as running after successful initialization
    g_recoveryState.devicesRunning = true;

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    std::cout << "\n[INFO] Bridge is running. Press Ctrl+C to quit.\n";
    std::cout << "[INFO] Automatic recovery enabled - bridge will attempt to recover from device failures.\n";

    while (g_keepRunning) {
        // Check if recovery is needed
        if (g_recoveryState.needsRecovery && !g_recoveryState.isRecovering) {
            int64_t currentTime = get_current_time_ms();
            int64_t timeSinceLastAttempt = currentTime - g_recoveryState.lastRecoveryAttemptMs;

            // Debounce: only attempt recovery if at least 2 seconds have passed
            if (timeSinceLastAttempt >= 2000) {
                attempt_recovery(&context, config, &sourceDevice, &targetDevice, &appData);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Restore normal priority when stopping
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    std::cout << "\n[INFO] Shutting down gracefully...\n";
    bool srcInit = g_recoveryState.sourceInitialized;
    bool tgtInit = g_recoveryState.targetInitialized;
    bool rbInit = g_recoveryState.ringBufferInitialized;
    cleanup_devices(&sourceDevice, &targetDevice, &appData, &srcInit, &tgtInit, &rbInit);
    ma_context_uninit(&context);

    return 0;
}
