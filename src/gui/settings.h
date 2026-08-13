#ifndef WB_GUI_SETTINGS_H
#define WB_GUI_SETTINGS_H

#include <string>

namespace wb {

struct Settings {
    std::wstring sourceDeviceId;
    std::wstring targetDeviceId;
    bool exclusiveMode = false;
    int sharedLatency = 10;
    int exclusiveLatency = 5;
    bool minimizeToTray = true;
};

Settings LoadSettings();
bool SaveSettings(const Settings& settings);

} // namespace wb

#endif // WB_GUI_SETTINGS_H
