#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::auto_super_weapon
{
    enum class Kind
    {
        IronCurtain,
        RageInductor
    };

    struct Cell
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;

        [[nodiscard]] bool operator==(const Cell&) const = default;
    };

    struct SelectedUnit
    {
        Cell Position;
        std::int32_t Cost = 0;
        std::vector<std::uint16_t> AttackableEnemies;
    };

    struct Snapshot
    {
        std::vector<SelectedUnit> Selected;
        std::vector<Cell> CandidateCenters;
        std::size_t EnemyCount = 0;
    };

    struct PlanResult
    {
        Cell Center;
        std::size_t CoveredUnits = 0;
        std::int64_t CoveredValue = 0;
        std::size_t AttackableEnemies = 0;
    };

    [[nodiscard]] bool Covers(Kind kind, Cell center, Cell unit) noexcept;
    [[nodiscard]] std::optional<PlanResult> Plan(
        Kind kind, const Snapshot& snapshot);
}
