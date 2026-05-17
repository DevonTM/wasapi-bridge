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
