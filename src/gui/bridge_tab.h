#ifndef WB_GUI_BRIDGE_TAB_H
#define WB_GUI_BRIDGE_TAB_H

#include <windows.h>

#include "app_state.h"
#include "settings.h"

namespace wb {

// Build the Bridge tab. Returns the panel HWND parented to hParent (the
// tab control). Controls inside are children of the panel.
HWND CreateBridgeTab(AppState* st, HWND hParent);

// Re-flow the Bridge tab controls to fit the given panel client size.
// Called on first paint and on every WM_SIZE forwarded by the panel proc.
void LayoutBridgeTab(AppState* st, int panelW, int panelH);

// Re-enumerate playback devices and reload both source/target combos.
// Tries to preserve the previous selection by device ID.
void RescanDevices(AppState* st);

// Re-enumerate after a device notification while preserving current choices.
// Missing selected devices remain visible as unavailable placeholders until
// their exact IDs return.
void AutoRescanDevices(AppState* st);

// Recompute the enabled/disabled state of inputs and the start button.
// Call after combo selection changes or when bridge state transitions.
void RefreshBridgeTabState(AppState* st);

// Apply settings after the initial device enumeration and persist current UI state.
void RestoreSettings(AppState* st, const Settings& settings);
void SaveCurrentSettings(AppState* st);

// Handle WM_COMMAND posted to the panel (forwarded by main window proc).
// Returns true if the command was handled.
bool HandleBridgeCommand(AppState* st, WORD ctrlId, WORD notifyCode);

// Returns the int currently in the latency edit box, clamped to 1..500.
// On parse failure, returns the default for the active mode.
int  ReadLatencyEdit(AppState* st);

// Sanitize and store the active latency edit value.
void CommitLatency(AppState* st);

// Push the latency value for the currently selected mode into the edit box.
void WriteLatencyEdit(AppState* st);

// Toggle Start <-> Stop based on the current bridge state.
void ToggleBridge(AppState* st);

} // namespace wb

#endif // WB_GUI_BRIDGE_TAB_H
