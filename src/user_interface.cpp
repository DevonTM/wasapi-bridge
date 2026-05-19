#include "user_interface.h"
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
struct DeviceEntry {
    ma_uint32 originalIndex; // index into pPlaybackInfos[]
    bool      supportsShared;
    bool      supportsExclusive;
};
}

bool prompt_user_configuration(ma_context* context, BridgeConfig* config) {
    ma_device_info* pPlaybackInfos;
    ma_uint32       playbackCount;

    if (ma_context_get_devices(context, &pPlaybackInfos, &playbackCount, NULL, NULL) != MA_SUCCESS) {
        std::cerr << "Failed to enumerate devices.\n";
        return false;
    }

    // Filter unsupported devices and remember the share modes each remaining
    // one advertises (per AGENTS.md: "Also filter out unsupported audio
    // devices."). Skip devices that report no native formats — those are
    // typically virtual or unavailable shells that miniaudio could not probe;
    // ma_device_init would fail later if we tried to use them. The
    // share-mode flags are kept internally and used to validate the user's
    // target+mode combination after the prompt completes.
    std::vector<DeviceEntry> usableDevices;
    usableDevices.reserve(playbackCount);

    for (ma_uint32 i = 0; i < playbackCount; ++i) {
        ma_device_info info = {};
        if (ma_context_get_device_info(context, ma_device_type_playback,
                                       &pPlaybackInfos[i].id, &info) != MA_SUCCESS) {
            continue;
        }
        if (info.nativeDataFormatCount == 0) {
            continue;
        }

        DeviceEntry entry{i, false, false};
        for (ma_uint32 f = 0; f < info.nativeDataFormatCount; ++f) {
            if (info.nativeDataFormats[f].flags & MA_DATA_FORMAT_FLAG_EXCLUSIVE_MODE) {
                entry.supportsExclusive = true;
            } else {
                entry.supportsShared = true;
            }
        }
        usableDevices.push_back(entry);
    }

    if (usableDevices.empty()) {
        std::cerr << "No usable playback devices found.\n";
        return false;
    }

    std::cout << "\nAvailable Playback Devices:\n";
    for (size_t i = 0; i < usableDevices.size(); ++i) {
        std::cout << i + 1 << ". "
                  << pPlaybackInfos[usableDevices[i].originalIndex].name << "\n";
    }

    // Helper: drain everything up to and including the next newline. Always
    // call this after a formatted `std::cin >> int` read so the stream is in
    // a clean state for any later read (whether `>>` or std::getline).
    const auto flushLine = [] {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    };

    const ma_uint32 deviceCount = static_cast<ma_uint32>(usableDevices.size());

    int sourceChoice = 0;
    int targetChoice = 0;

    // Device selection
    while (true) {
        while (true) {
            std::cout << "\nSelect SOURCE audio device (Loopback): ";
            const bool ok = static_cast<bool>(std::cin >> sourceChoice);
            if (!ok) std::cin.clear();
            flushLine();
            if (ok && sourceChoice >= 1 && static_cast<ma_uint32>(sourceChoice) <= deviceCount) break;
            std::cout << "Invalid choice. Please enter a valid number.\n";
        }

        while (true) {
            std::cout << "Select TARGET audio device (Output): ";
            const bool ok = static_cast<bool>(std::cin >> targetChoice);
            if (!ok) std::cin.clear();
            flushLine();
            if (ok && targetChoice >= 1 && static_cast<ma_uint32>(targetChoice) <= deviceCount) break;
            std::cout << "Invalid choice. Please enter a valid number.\n";
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
        const bool ok = static_cast<bool>(std::cin >> modeChoice);
        if (!ok) std::cin.clear();
        flushLine();
        if (ok && (modeChoice == 1 || modeChoice == 2)) break;
        std::cout << "Invalid choice. Please enter 1 or 2.\n";
    }
    const ma_share_mode shareMode = (modeChoice == 2) ? ma_share_mode_exclusive : ma_share_mode_shared;

    // Validate target against the chosen share mode. The filtered device
    // list ensures every entry has at least one mode, but a user can still
    // pick exclusive against a target that only advertises shared (or vice
    // versa). Catch that here so we fail with a directed message instead of
    // letting ma_device_init fail downstream.
    const auto& targetEntry = usableDevices[targetChoice - 1];
    if (shareMode == ma_share_mode_exclusive && !targetEntry.supportsExclusive) {
        std::cerr << "TARGET device does not advertise exclusive mode in its current Windows configuration.\n";
        std::cerr << "Either pick a different target or choose Shared mode.\n";
        return false;
    }
    if (shareMode == ma_share_mode_shared && !targetEntry.supportsShared) {
        std::cerr << "TARGET device does not advertise shared mode.\n";
        return false;
    }

    // Latency configuration. Stream is already flushed after the mode read
    // above, so getline starts cleanly.
    uint32_t defaultLatency = (shareMode == ma_share_mode_exclusive) ? 5 : 10;
    uint32_t targetLatency = defaultLatency;

    while (true) {
        std::cout << "\nEnter desired target latency in milliseconds (default " << defaultLatency << "): ";
        std::string latencyInput;
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

    // Store configuration. Map filtered-list indices back to the original
    // pPlaybackInfos[] entries so we hand miniaudio the real device IDs.
    const auto& sourceEntry = usableDevices[sourceChoice - 1];
    config->sourceDeviceId = pPlaybackInfos[sourceEntry.originalIndex].id;
    config->targetDeviceId = pPlaybackInfos[targetEntry.originalIndex].id;
    config->shareMode = shareMode;
    config->targetLatency = targetLatency;

    return true;
}
