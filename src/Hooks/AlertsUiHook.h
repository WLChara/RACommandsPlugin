#pragma once

#include <cstdint>
#include <string>

namespace ra_commands::game
{
    using BeaconPlacedCallback = void(*)(int owner, int slot);
    using RadarAcceptedCallback = void(*)(int type, std::int16_t cellX, std::int16_t cellY);

    bool InstallBeaconCaptureHook(BeaconPlacedCallback callback, std::string& outError);
    bool InstallRadarCaptureHook(RadarAcceptedCallback callback, std::string& outError);
    void SetAlertsUiGameThread(std::uint32_t threadId) noexcept;
    void DisableAlertsUiHooks() noexcept;
}
