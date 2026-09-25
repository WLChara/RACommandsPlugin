#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::auto_nano_cloud
{
    struct UnitId
    {
        std::uintptr_t Address = 0;
        std::uint32_t UniqueId = 0;

        [[nodiscard]] bool operator==(const UnitId&) const = default;
    };

    struct HunterSnapshot
    {
        UnitId Id;
        int PrimaryDamage = 0;
        std::int32_t CellX = 0;
        std::int32_t CellY = 0;
    };

    struct VictimSnapshot
    {
        UnitId Id;
        int Cost = 0;
        int Health = 0;
        std::int32_t CellX = 0;
        std::int32_t CellY = 0;
    };

    struct Snapshot
    {
        std::vector<HunterSnapshot> Hunters;
        std::vector<VictimSnapshot> SelectedVictims;
        std::optional<VictimSnapshot> RetainedVictim;
    };

    struct PlanResult
    {
        UnitId Victim;
        std::vector<UnitId> Hunters;
        bool UsesRetainedVictim = false;
    };

    [[nodiscard]] constexpr bool CanSacrificeBuildLimit(int buildLimit) noexcept
    {
        return buildLimit != 1;
    }

    /** 仅使用当前值快照选择低损失目标和本轮参战 HUNTR，不访问游戏对象。 */
    [[nodiscard]] std::optional<PlanResult> Plan(const Snapshot& snapshot);
}
