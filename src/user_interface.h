#ifndef WB_USER_INTERFACE_H
#define WB_USER_INTERFACE_H

#include "types.h"
#include "miniaudio.h"

// Prompt user to select audio devices and configure settings
bool prompt_user_configuration(ma_context* context, BridgeConfig* config);

#endif // WB_USER_INTERFACE_H
