#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <windows.h>

#include "miniaudio.h"

#define WB_VERSION "0.1.3"

struct ApplicationData {
    ma_pcm_rb ringBuffer;
    ma_uint32 sourceChannels;
    ma_uint32 targetChannels;
};

std::atomic<bool> g_keepRunning{true};

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        g_keepRunning = false;
        return TRUE;
    }
    return FALSE;
}

void cleanup(ma_device* sourceDevice, ma_device* targetDevice, ApplicationData* appData) {
    if (sourceDevice) ma_device_uninit(sourceDevice);
    if (targetDevice) ma_device_uninit(targetDevice);
    if (appData) ma_pcm_rb_uninit(&appData->ringBuffer);
}

void capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    ApplicationData* appData = (ApplicationData*)pDevice->pUserData;
    if (pInput == nullptr) return;

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
    if (pOutput == nullptr) return;

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

    ma_device_id sourceDeviceId = pPlaybackInfos[sourceChoice - 1].id;
    ma_device_id targetDeviceId = pPlaybackInfos[targetChoice - 1].id;

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

    ma_device sourceDevice;
    ma_device targetDevice;
    ApplicationData appData = {};

    // Get source device info for channel/sample rate matching
    ma_device_info sourceInfo = pPlaybackInfos[sourceChoice - 1];

    // For simplicity, hardcoded default sample rate, but we can extract more using an intermediary dummy device or context info.
    appData.sourceChannels = 2; // Default, actual channels handled by device config
    appData.targetChannels = 2;

    ma_pcm_rb_init(ma_format_f32, appData.targetChannels, 44100 * 2, NULL, NULL, &appData.ringBuffer); // 2 second buffer

    ma_device_config sourceConfig = ma_device_config_init(ma_device_type_loopback);
    sourceConfig.capture.pDeviceID = &sourceDeviceId;
    sourceConfig.capture.format = ma_format_f32;
    sourceConfig.dataCallback = capture_callback;
    sourceConfig.pUserData = &appData;
    // Prefer low-latency defaults
    sourceConfig.performanceProfile = ma_performance_profile_low_latency;
    // Tell WASAPI we are doing pro-audio to elevate MMCSS thread priority
    sourceConfig.wasapi.usage = ma_wasapi_usage_pro_audio;

    if (ma_device_init(&context, &sourceConfig, &sourceDevice) != MA_SUCCESS) {
        std::cerr << "Failed to init source loopback device\n";
        cleanup(NULL, NULL, &appData);
        ma_context_uninit(&context);
        return -1;
    }

    appData.sourceChannels = sourceDevice.capture.channels;

    std::cout << "\nSource Loopback successfully initialized at " << sourceDevice.sampleRate << " Hz\n";

    ma_device_config targetConfig = ma_device_config_init(ma_device_type_playback);
    targetConfig.playback.pDeviceID = &targetDeviceId;
    targetConfig.playback.format = ma_format_f32;
    // Keep target sample rate the same as the source to avoid resampling artifacts
    targetConfig.sampleRate = sourceDevice.sampleRate;
    targetConfig.playback.shareMode = shareMode;
    targetConfig.periodSizeInMilliseconds = targetLatency;
    targetConfig.dataCallback = playback_callback;
    targetConfig.pUserData = &appData;
    // Prefer low-latency defaults
    targetConfig.performanceProfile = ma_performance_profile_low_latency;
    // Tell WASAPI we are doing pro-audio to elevate MMCSS thread priority
    targetConfig.wasapi.usage = ma_wasapi_usage_pro_audio;

    if (ma_device_init(&context, &targetConfig, &targetDevice) != MA_SUCCESS) {
        std::cerr << "Failed to init target device.\n";
        cleanup(&sourceDevice, NULL, &appData);
        ma_context_uninit(&context);
        return -1;
    }

    appData.targetChannels = targetDevice.playback.channels;

    // Restart ring buffer with accurate target channels
    ma_pcm_rb_uninit(&appData.ringBuffer);
    ma_pcm_rb_init(ma_format_f32, appData.targetChannels, sourceDevice.sampleRate * 2, NULL, NULL, &appData.ringBuffer);

    std::cout << "Starting stream...\n";
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Elevate process priority only when streaming actually starts
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    ma_device_start(&sourceDevice);
    ma_device_start(&targetDevice);

    std::cout << "Streaming running... Press Ctrl+C or close the window to quit.\n";
    while (g_keepRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Restore normal priority when stopping
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    std::cout << "\nShutting down gracefully...\n";
    cleanup(&sourceDevice, &targetDevice, &appData);
    ma_context_uninit(&context);

    return 0;
}
