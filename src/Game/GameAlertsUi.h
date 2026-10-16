#pragma once

#include "UI/Overlay/HudTypes.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ra_commands::game
{
    /** 两种提示独立保持能力状态；目标地址验证失败时不接管对应功能。 */
    bool InstallGameAlertsUi(std::string& outError);
    void UpdateGameAlertsUi(std::uint64_t nowMs, bool isSessionActive, std::uintptr_t sessionIdentity);
    std::vector<ui::BeaconNotice> CaptureBeaconNotices(std::uint64_t nowMs);
    std::vector<ui::RadarNotice> CaptureRadarNotices();
    bool TryJumpToRadarNotice(std::uint64_t noticeId);
    bool ToggleRadarNoticePin(std::uint64_t noticeId);
    bool AreBeaconIndicatorsEnabled() noexcept;
    bool IsRadarFeedEnabled() noexcept;
    void SetBeaconIndicatorsEnabled(bool isEnabled);
    void SetRadarFeedEnabled(bool isEnabled);
    void DisableGameAlertsUi() noexcept;
}
