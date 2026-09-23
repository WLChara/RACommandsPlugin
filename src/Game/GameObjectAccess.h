#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

#include <cstdint>

class TechnoClass;

namespace ra_commands::game
{
    // 仅在游戏线程访问；返回的指针不得跨帧缓存。
    [[nodiscard]] bool IsGameSessionReady();
    [[nodiscard]] bool IsTeslaChargeSessionReady();
    [[nodiscard]] TechnoClass* FindLiveTechno(std::uint64_t uniqueId);
    [[nodiscard]] TechnoClass* ResolveIdentity(
        const commands::ClickedMissionIdentity& identity);
    [[nodiscard]] commands::ClickedMissionIdentity CaptureIdentity(
        TechnoClass* techno,
        std::uint32_t epoch);
    [[nodiscard]] bool CanEnterTransport(TechnoClass* passenger, TechnoClass* transport);
    [[nodiscard]] bool IsLocalTesla(TechnoClass* techno);
    [[nodiscard]] bool IsLocalTeslaCharger(TechnoClass* techno);
    [[nodiscard]] bool CanChargeTesla(TechnoClass* charger, TechnoClass* tesla);
    [[nodiscard]] std::uint32_t GetCurrentGameFrame();
    [[nodiscard]] std::int32_t GetGameFrameSendRate();
}
