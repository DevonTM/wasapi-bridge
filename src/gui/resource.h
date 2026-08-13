#ifndef WB_RESOURCE_H
#define WB_RESOURCE_H

// Shared between wasapi-bridge.rc and the GUI source so resource IDs stay
// in lockstep. Numbering follows the conventional ranges:
//   1..99    icons / images
//   100..999 dialog / control IDs
//   1000+    menu commands

#define IDI_APP_ICON                1

// Tab indices for the main TabControl.
#define IDC_TAB_MAIN                100

// Bridge tab controls (200..299).
#define IDC_BRIDGE_PANEL            200
#define IDC_LBL_SOURCE              201
#define IDC_CMB_SOURCE              202
#define IDC_LBL_TARGET              203
#define IDC_CMB_TARGET              204
#define IDC_BTN_RESCAN              205
#define IDC_GRP_DEVICES             206
#define IDC_GRP_MODE                207
#define IDC_RAD_SHARED              208
#define IDC_RAD_EXCLUSIVE           209
#define IDC_GRP_LATENCY             210
#define IDC_EDT_LATENCY             211
#define IDC_LBL_LATENCY_UNIT        212
#define IDC_LBL_LATENCY_HINT        213
#define IDC_LBL_STATE               215
#define IDC_BTN_TOGGLE              216
#define IDC_CHK_MIN_TRAY            217
#define IDC_LBL_STATE_DOT           218

// Log tab controls (300..399).
#define IDC_LOG_PANEL               300
#define IDC_EDT_LOG                 301
#define IDC_CHK_AUTOSCROLL          302
#define IDC_BTN_EXPORT_LOG          303
#define IDC_BTN_CLEAR_LOG           304

// About tab controls (400..499).
#define IDC_ABOUT_PANEL             400
#define IDC_STATIC_ICON             401
#define IDC_LBL_APP_NAME            402
#define IDC_LBL_APP_VERSION         403
#define IDC_LNK_LICENSE             404
#define IDC_BTN_GITHUB              405
#define IDC_BTN_RESET_SETTINGS      406

// Tray menu commands (1000+).
#define IDM_TRAY_OPEN               1001
#define IDM_TRAY_EXIT               1002

#endif // WB_RESOURCE_H
