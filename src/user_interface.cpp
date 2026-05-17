#include "user_interface.h"
#include <iostream>
#include <string>

bool prompt_user_configuration(ma_context* context, BridgeConfig* config) {
    ma_device_info* pPlaybackInfos;
    ma_uint32 playbackCount;

    if (ma_context_get_devices(context, &pPlaybackInfos, &playbackCount, NULL, NULL) != MA_SUCCESS) {
        std::cerr << "Failed to enumerate devices.\n";
        return false;
    }

    std::cout << "\nAvailable Playback Devices:\n";
    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        std::cout << i + 1 << ". " << pPlaybackInfos[i].name << "\n";
    }

    int sourceChoice = 0;
    int targetChoice = 0;

    // Device selection
    while (true) {
        while (true) {
            std::cout << "\nSelect SOURCE audio device (Loopback): ";
            if (std::cin >> sourceChoice && sourceChoice >= 1 && static_cast<ma_uint32>(sourceChoice) <= playbackCount) break;
            std::cout << "Invalid choice. Please enter a valid number.\n";
            std::cin.clear();
            std::cin.ignore(10000, '\n');
        }

        while (true) {
            std::cout << "Select TARGET audio device (Output): ";
            if (std::cin >> targetChoice && targetChoice >= 1 && static_cast<ma_uint32>(targetChoice) <= playbackCount) break;
            std::cout << "Invalid choice. Please enter a valid number.\n";
            std::cin.clear();
            std::cin.ignore(10000, '\n');
        }

        if (sourceChoice != targetChoice) {
            break;
        }
        std::cout << "SOURCE and TARGET cannot be the same device. Please select again.\n";
    }

    // WASAPI mode selection
    std::cout << "\nWASAPI Modes:\n1. Shared\n2. Exclusive\n";
    int modeChoice = 0;
    while (true) {
        std::cout << "Select Mode for TARGET device: ";
        if (std::cin >> modeChoice && (modeChoice == 1 || modeChoice == 2)) break;
        std::cout << "Invalid choice. Please enter 1 or 2.\n";
        std::cin.clear();
        std::cin.ignore(10000, '\n');
    }
    ma_share_mode shareMode = (modeChoice == 2) ? ma_share_mode_exclusive : ma_share_mode_shared;

    // Latency configuration
    uint32_t defaultLatency = (shareMode == ma_share_mode_exclusive) ? 5 : 10;
    uint32_t targetLatency = defaultLatency;

    while (true) {
        std::cout << "\nEnter desired target latency in milliseconds (default " << defaultLatency << "): ";
        std::string latencyInput;
        std::cin.ignore();
        std::getline(std::cin, latencyInput);

        if (latencyInput.empty()) {
            targetLatency = defaultLatency;
            std::cout << "Using default latency " << targetLatency << " ms\n";
            break;
        }

        try {
            int latencyValue = std::stoi(latencyInput);
            if (latencyValue < 0) {
                std::cout << "Latency cannot be negative. Please enter a valid positive number.\n";
                continue;
            }
            if (latencyValue == 0) {
                targetLatency = defaultLatency;
                std::cout << "Using default latency " << targetLatency << " ms\n";
                break;
            }
            targetLatency = static_cast<uint32_t>(latencyValue);
            std::cout << "Latency set to " << targetLatency << " ms\n";
            break;
        } catch(...) {
            std::cout << "Invalid input. Please enter a valid positive number.\n";
        }
    }

    // Store configuration
    config->sourceDeviceId = pPlaybackInfos[sourceChoice - 1].id;
    config->targetDeviceId = pPlaybackInfos[targetChoice - 1].id;
    config->shareMode = shareMode;
    config->targetLatency = targetLatency;

    return true;
}
