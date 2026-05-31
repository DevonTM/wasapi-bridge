#include "device_enum.h"

#include "logger.h"

namespace wb {

std::vector<DeviceEntry> EnumeratePlaybackDevices() {
    std::vector<DeviceEntry> result;

    ma_context ctx{};
    ma_context_config cfg = ma_context_config_init();
    ma_backend backend = ma_backend_wasapi;
    if (ma_result rc = ma_context_init(&backend, 1, &cfg, &ctx); rc != MA_SUCCESS) {
        WB_LOG_ERROR("Device enumeration: ma_context_init failed (%d)", static_cast<int>(rc));
        return result;
    }

    ma_device_info* infos = nullptr;
    ma_uint32       count = 0;
    if (ma_context_get_devices(&ctx, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        WB_LOG_ERROR("Device enumeration: ma_context_get_devices failed");
        ma_context_uninit(&ctx);
        return result;
    }

    result.reserve(count);
    for (ma_uint32 i = 0; i < count; ++i) {
        ma_device_info detailed{};
        if (ma_context_get_device_info(&ctx, ma_device_type_playback, &infos[i].id, &detailed) != MA_SUCCESS) {
            continue;
        }
        if (detailed.nativeDataFormatCount == 0) {
            continue;
        }

        DeviceEntry entry;
        entry.name = infos[i].name;
        entry.id   = infos[i].id;
        for (ma_uint32 f = 0; f < detailed.nativeDataFormatCount; ++f) {
            if (detailed.nativeDataFormats[f].flags & MA_DATA_FORMAT_FLAG_EXCLUSIVE_MODE) {
                entry.supportsExclusive = true;
            } else {
                entry.supportsShared = true;
            }
        }
        if (!entry.supportsShared && !entry.supportsExclusive) {
            continue;
        }
        result.push_back(std::move(entry));
    }

    ma_context_uninit(&ctx);
    return result;
}

} // namespace wb
