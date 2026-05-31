#ifndef WB_GUI_DEVICE_ENUM_H
#define WB_GUI_DEVICE_ENUM_H

#include <string>
#include <vector>

#include "miniaudio.h"

namespace wb {

// Snapshot of one playback device suitable for display in a combobox.
// id is owned by this struct so the GUI can keep entries alive even after
// the enumeration's ma_context goes away.
struct DeviceEntry {
    std::string  name;
    ma_device_id id;
    bool         supportsShared    = false;
    bool         supportsExclusive = false;
};

// Enumerate available playback devices and return only the ones that
// (a) reported at least one native data format and (b) advertise either
// shared or exclusive mode. Mirrors the filtering logic that used to live in
// user_interface.cpp so devices the bridge can't actually use never appear in
// the UI. Internally creates and tears down its own ma_context, so it's safe
// to call from the GUI thread without coordinating with the bridge worker.
//
// Returns an empty vector on failure (errors are logged via Logger).
std::vector<DeviceEntry> EnumeratePlaybackDevices();

} // namespace wb

#endif // WB_GUI_DEVICE_ENUM_H
