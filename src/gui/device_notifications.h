#ifndef WB_GUI_DEVICE_NOTIFICATIONS_H
#define WB_GUI_DEVICE_NOTIFICATIONS_H

#include <windows.h>

namespace wb {

// Registers a Core Audio endpoint notification callback. Callback methods only
// post a message; enumeration and control updates remain on the GUI thread.
class DeviceNotificationMonitor {
public:
    DeviceNotificationMonitor() = default;
    ~DeviceNotificationMonitor();

    DeviceNotificationMonitor(const DeviceNotificationMonitor&) = delete;
    DeviceNotificationMonitor& operator=(const DeviceNotificationMonitor&) = delete;

    bool Start(HWND notifyWindow, UINT notifyMessage);
    void Stop();

private:
    class Client;
    Client* client_ = nullptr;
    void* enumerator_ = nullptr;
    bool comInitialized_ = false;
};

} // namespace wb

#endif // WB_GUI_DEVICE_NOTIFICATIONS_H
