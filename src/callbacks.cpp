#include "callbacks.h"
#include "types.h"
#include "gui/logger.h"
#include <algorithm>
#include <cstring>

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
            WB_LOG_NOTIFY("%s device stopped unexpectedly", deviceType);
            g_recoveryState.devicesRunning = false;
            wakeMainLoop();
            break;

        case ma_device_notification_type_rerouted:
            WB_LOG_NOTIFY("%s device rerouted", deviceType);
            g_recoveryState.devicesRunning = false;
            wakeMainLoop();
            break;

        case ma_device_notification_type_interruption_began:
            WB_LOG_NOTIFY("%s device interruption began", deviceType);
            g_recoveryState.devicesRunning = false;
            wakeMainLoop();
            break;

        case ma_device_notification_type_interruption_ended:
            WB_LOG_NOTIFY("%s device interruption ended", deviceType);
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
    ApplicationData* appData = static_cast<ApplicationData*>(pDevice->pUserData);
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

        const float* pSrc = static_cast<const float*>(pInput) + static_cast<size_t>(framesWritten) * sourceChannels;
        float* pDst = static_cast<float*>(pWriteBuffer);

        // Source -> target channel mapping. Per AGENTS.md: positional map,
        // drop unmapped source channels, zero unmapped target channels.
        // Hard clamp to [-1.0, 1.0] for peak clipping.
        if (sourceChannels == targetChannels) {
            // Fast path: layouts match, contiguous copy + clamp.
            const ma_uint32 totalSamples = chunk * targetChannels;
            for (ma_uint32 i = 0; i < totalSamples; ++i) {
                pDst[i] = std::clamp(pSrc[i], -1.0f, 1.0f);
            }
        } else {
            // Differing channel counts: branch hoisted out of the inner loop.
            const ma_uint32 mappedChannels = std::min(sourceChannels, targetChannels);
            const ma_uint32 zeroChannels   = targetChannels - mappedChannels;
            for (ma_uint32 i = 0; i < chunk; ++i) {
                const float* pFrameSrc = pSrc + static_cast<size_t>(i) * sourceChannels;
                float*       pFrameDst = pDst + static_cast<size_t>(i) * targetChannels;
                for (ma_uint32 c = 0; c < mappedChannels; ++c) {
                    pFrameDst[c] = std::clamp(pFrameSrc[c], -1.0f, 1.0f);
                }
                for (ma_uint32 c = 0; c < zeroChannels; ++c) {
                    pFrameDst[mappedChannels + c] = 0.0f;
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
    ApplicationData* appData = static_cast<ApplicationData*>(pDevice->pUserData);
    if (pOutput == nullptr || appData == nullptr) return;

    // Load atomic channel count once for consistency
    ma_uint32 targetChannels = appData->targetChannels.load();

    float* pDst = static_cast<float*>(pOutput);

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

        const float* pSrc = static_cast<const float*>(pReadBuffer);
        float* pCursor = pDst + static_cast<size_t>(framesRead) * targetChannels;
        std::memcpy(pCursor, pSrc, static_cast<size_t>(chunk) * targetChannels * sizeof(float));

        ma_pcm_rb_commit_read(&appData->ringBuffer, chunk);

        framesRead += chunk;
        remaining  -= chunk;
    }

    // Fill remainder with zeroes stringently (handle underflow). memset of
    // all-zero bytes is +0.0f for IEEE-754 floats.
    if (framesRead < frameCount) {
        const ma_uint32 zeroes = (frameCount - framesRead) * targetChannels;
        float* pTail = pDst + static_cast<size_t>(framesRead) * targetChannels;
        std::memset(pTail, 0, static_cast<size_t>(zeroes) * sizeof(float));
    }
}
