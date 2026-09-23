#pragma once

#include <cstdint>
#include <vector>

namespace ra_commands::tesla_charge
{
    using UnitId = std::uint64_t;

    struct ObjectSnapshot
    {
        UnitId Id = 0;
        std::uintptr_t Owner = 0;
        std::int32_t CellX = 0;
        std::int32_t CellY = 0;
    };

    struct Snapshot
    {
        std::uintptr_t LocalOwner = 0;
        std::vector<ObjectSnapshot> Teslas;
        std::vector<ObjectSnapshot> Chargers;
    };

    struct Assignment
    {
        UnitId Tesla = 0;
        UnitId Charger = 0;

        bool operator==(const Assignment&) const = default;
    };

    // 保留仍有效的配对，再按线圈顺序为缺口选择 32 格内最近的空闲充能兵。
    [[nodiscard]] std::vector<Assignment> Plan(
        const Snapshot& snapshot,
        const std::vector<Assignment>& previous);
}
