#include <iostream>
#include <thread>
#include <chrono>
#include <windows.h>

#include "version.h"
#include "types.h"
#include "callbacks.h"
#include "device_manager.h"
#include "user_interface.h"

// Global state definitions
std::atomic<bool> g_keepRunning{true};
RecoveryState g_recoveryState;
std::mutex g_wakeupMutex;
std::condition_variable g_wakeupCv;

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_BREAK_EVENT) {
        g_keepRunning = false;
        g_wakeupCv.notify_one();
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char** argv) {
    SetConsoleTitleA("WASAPI Bridge");
    std::cout << "Starting WASAPI Bridge v" << WB_VERSION << "\n";

    // Initialize miniaudio context
    ma_context context;
    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.threadPriority = ma_thread_priority_realtime;

    ma_backend backend = ma_backend_wasapi;
    if (ma_context_init(&backend, 1, &contextConfig, &context) != MA_SUCCESS) {
        std::cerr << "Failed to initialize standard WASAPI context.\n";
        return -1;
    }

    // Prompt user for configuration
    BridgeConfig config;
    if (!prompt_user_configuration(&context, &config)) {
        std::cerr << "Failed to configure bridge.\n";
        ma_context_uninit(&context);
        return -1;
    }

    // Initialize devices and application data
    ma_device sourceDevice;
    ma_device targetDevice;
    ApplicationData appData = {};

    std::cout << "\n[INFO] Initializing bridge...\n";
    if (!initialize_bridge(&context, config, &sourceDevice, &targetDevice, &appData)) {
        std::cerr << "[ERROR] Failed to initialize bridge\n";
        ma_context_uninit(&context);
        return -1;
    }

    // Mark devices as running after successful initialization
    g_recoveryState.devicesRunning = true;

    // Set up console control handler and process priority
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    std::cout << "\n[INFO] Bridge is running. Press Ctrl+C to quit.\n";
    std::cout << "[INFO] Automatic recovery enabled - bridge will attempt to recover from device failures.\n";

    // Main loop with recovery handling. Waits on a condition variable so we
    // wake up immediately on Ctrl+C or a device-state notification instead of
    // riding out the poll timeout. The 100 ms timeout stays as a safety belt
    // and as the polling cadence for the 3-second recovery debounce.
    while (g_keepRunning) {
        // Check if recovery is needed
        if (g_recoveryState.needsRecovery && !g_recoveryState.isRecovering) {
            int64_t currentTime = get_current_time_ms();
            int64_t timeSinceLastAttempt = currentTime - g_recoveryState.lastRecoveryAttemptMs;

            // Debounce: only attempt recovery if at least 3 seconds have passed
            if (timeSinceLastAttempt >= 3000) {
                attempt_recovery(&context, config, &sourceDevice, &targetDevice, &appData);
            }
        }

        std::unique_lock<std::mutex> lock(g_wakeupMutex);
        g_wakeupCv.wait_for(lock, std::chrono::milliseconds(100), [] {
            return !g_keepRunning ||
                   (g_recoveryState.needsRecovery && !g_recoveryState.isRecovering);
        });
    }

    // Restore normal priority when stopping
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    // Cleanup
    std::cout << "\n[INFO] Shutting down gracefully...\n";
    g_recoveryState.devicesRunning = false;
    bool srcInit = g_recoveryState.sourceInitialized;
    bool tgtInit = g_recoveryState.targetInitialized;
    bool rbInit = g_recoveryState.ringBufferInitialized;
    cleanup_devices(&sourceDevice, &targetDevice, &appData, &srcInit, &tgtInit, &rbInit);
    ma_context_uninit(&context);

    return 0;
}
