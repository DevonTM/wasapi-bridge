#ifndef WB_DEVICE_MANAGER_H
#define WB_DEVICE_MANAGER_H

#include "types.h"
#include "miniaudio.h"

// Get current time in milliseconds
int64_t get_current_time_ms();

// Clean up devices and ring buffer
void cleanup_devices(ma_device* sourceDevice, ma_device* targetDevice,
                     ApplicationData* appData, bool* sourceInitialized,
                     bool* targetInitialized, bool* ringBufferInitialized);

// Initialize the audio bridge
bool initialize_bridge(ma_context* context, const BridgeConfig& config,
                       ma_device* sourceDevice, ma_device* targetDevice,
                       ApplicationData* appData);

// Attempt to recover from device failure
bool attempt_recovery(ma_context* context, const BridgeConfig& config,
                      ma_device* sourceDevice, ma_device* targetDevice,
                      ApplicationData* appData);

#endif // WB_DEVICE_MANAGER_H
