#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

#include <cstdint>
#include <string_view>

class TechnoClass;

namespace ra_commands::game
{
    // 仅在游戏线程访问；返回的指针不得跨帧缓存。
    [[nodiscard]] bool IsGameSessionReady();
    [[nodiscard]] bool IsGameSessionWithBuildingsReady();
    // IsLocal 仅判断所有权；存活、在地图上等条件由功能自行组合。
    [[nodiscard]] bool IsLocal(const TechnoClass* techno);
    [[nodiscard]] bool NameEqual(const TechnoClass* techno, std::string_view registeredName);
    [[nodiscard]] TechnoClass* FindLiveTechno(std::uint64_t uniqueId);
    [[nodiscard]] TechnoClass* ResolveIdentity(
        const commands::ClickedMissionIdentity& identity);
    [[nodiscard]] commands::ClickedMissionIdentity CaptureIdentity(
        TechnoClass* techno,
        std::uint32_t epoch);
    [[nodiscard]] bool CanEnterTransport(TechnoClass* passenger, TechnoClass* transport);
    [[nodiscard]] std::uint32_t GetCurrentGameFrame();
    [[nodiscard]] std::int32_t GetGameFrameSendRate();
}
