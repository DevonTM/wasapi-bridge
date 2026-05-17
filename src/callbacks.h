#ifndef WB_CALLBACKS_H
#define WB_CALLBACKS_H

#include "miniaudio.h"

// Device notification callback
void device_notification_callback(const ma_device_notification* pNotification);

// Audio capture callback (loopback)
void capture_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

// Audio playback callback
void playback_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

#endif // WB_CALLBACKS_H
