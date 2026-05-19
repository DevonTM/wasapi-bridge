#include "callbacks.h"
#include "types.h"
#include <iostream>
#include <algorithm>

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

    // Wakes the main loop immediately when recovery is needed. notify_one is
    // safe to call without holding g_wakeupMutex; the atomic flag carries the
    // signal value, the CV is just the wakeup mechanism. This honours the
    // "no heavyweight locks in the notification callback" rule since miniaudio
    // can call this from a WASAPI thread.
    auto wakeMainLoop = [] {
        g_recoveryState.needsRecovery = true;
        g_wakeupCv.notify_one();
    };

    switch (pNotification->type) {
        case ma_device_notification_type_stopped:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device stopped unexpectedly\n";
            g_recoveryState.devicesRunning = false;
            wakeMainLoop();
            break;

        case ma_device_notification_type_rerouted:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device rerouted\n";
            g_recoveryState.devicesRunning = false;
            wakeMainLoop();
            break;

        case ma_device_notification_type_interruption_began:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device interruption began\n";
            g_recoveryState.devicesRunning = false;
            wakeMainLoop();
            break;

        case ma_device_notification_type_interruption_ended:
            std::cout << "\n[NOTIFICATION] " << deviceType << " device interruption ended\n";
            wakeMainLoop();
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

void capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;
    ApplicationData* appData = (ApplicationData*)pDevice->pUserData;
    if (pInput == nullptr || appData == nullptr) return;

    // Load atomic channel counts once for consistency
    ma_uint32 sourceChannels = appData->sourceChannels.load();
    ma_uint32 targetChannels = appData->targetChannels.load();

    // Loop because ma_pcm_rb_acquire_write may short-return at the sub-buffer
    // boundary. Without this loop the leftover frames are silently dropped,
    // producing intermittent clicks.
    ma_uint32 remaining = frameCount;
    ma_uint32 framesWritten = 0;

    while (remaining > 0) {
        ma_uint32 chunk = remaining;
        void* pWriteBuffer = nullptr;

        if (ma_pcm_rb_acquire_write(&appData->ringBuffer, &chunk, &pWriteBuffer) != MA_SUCCESS) break;
        if (chunk == 0) break; // Ring buffer full

        const float* pSrc = (const float*)pInput + (size_t)framesWritten * sourceChannels;
        float* pDst = (float*)pWriteBuffer;

        // Copy logic for source -> target mapping
        for (ma_uint32 i = 0; i < chunk; ++i) {
            for (ma_uint32 c = 0; c < targetChannels; ++c) {
                if (c < sourceChannels) {
                    float sample = pSrc[i * sourceChannels + c];
                    // Handle peak clipping correctly (Hard clamp to valid float audio range [-1.0, 1.0])
                    pDst[i * targetChannels + c] = std::clamp(sample, -1.0f, 1.0f);
                } else {
                    pDst[i * targetChannels + c] = 0.0f;
                }
            }
        }

        ma_pcm_rb_commit_write(&appData->ringBuffer, chunk);

        framesWritten += chunk;
        remaining     -= chunk;
    }
}

void playback_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;
    ApplicationData* appData = (ApplicationData*)pDevice->pUserData;
    if (pOutput == nullptr || appData == nullptr) return;

    // Load atomic channel count once for consistency
    ma_uint32 targetChannels = appData->targetChannels.load();

    float* pDst = (float*)pOutput;

    // Loop because ma_pcm_rb_acquire_read may short-return at the sub-buffer
    // boundary. Without this loop the leftover frames are silently dropped,
    // producing intermittent clicks.
    ma_uint32 remaining = frameCount;
    ma_uint32 framesRead = 0;

    while (remaining > 0) {
        ma_uint32 chunk = remaining;
        void* pReadBuffer = nullptr;

        if (ma_pcm_rb_acquire_read(&appData->ringBuffer, &chunk, &pReadBuffer) != MA_SUCCESS) break;
        if (chunk == 0) break; // Ring buffer empty (underflow)

        const float* pSrc = (const float*)pReadBuffer;
        float* pCursor = pDst + (size_t)framesRead * targetChannels;
        for (ma_uint32 i = 0; i < chunk * targetChannels; ++i) {
            pCursor[i] = pSrc[i];
        }

        ma_pcm_rb_commit_read(&appData->ringBuffer, chunk);

        framesRead += chunk;
        remaining  -= chunk;
    }

    // Fill remainder with zeroes stringently (handle underflow)
    if (framesRead < frameCount) {
        ma_uint32 zeroes = (frameCount - framesRead) * targetChannels;
        float* pTail = pDst + (size_t)framesRead * targetChannels;
        for (ma_uint32 i = 0; i < zeroes; ++i) {
            pTail[i] = 0.0f;
        }
    }
}
