#pragma once

#include <cstdint>
#include <vector>

namespace ra_commands::air_spread
{
    using ActorId = std::uint64_t;

    enum class ActorKind
    {
        FlyingInfantry,
        FlyingUnit,
        Aircraft
    };

    struct Cell
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;

        bool operator==(const Cell&) const = default;
    };

    struct ActorCandidates
    {
        ActorId Id = 0;
        ActorKind Kind = ActorKind::FlyingInfantry;
        // 由调用方按接近目标格的顺序提供，且只包含通行有效的格。
        std::vector<Cell> Cells;
    };

    struct CellOccupancy
    {
        Cell Location;
        std::uint32_t Count = 0;
    };

    struct Assignment
    {
        ActorId Actor = 0;
        ActorKind Kind = ActorKind::FlyingInfantry;
        Cell Destination;

        bool operator==(const Assignment&) const = default;
    };

    struct PlanResult
    {
        std::vector<Assignment> Assignments;
        std::vector<ActorId> UnassignedActors;
    };

    /**
     * 按输入顺序将 actor 分配至首个尚有容量的候选格。
     *
     * @param actors 每个 actor 的候选格须已按接近目标格排序，且只包含通行有效格。
     *               重复 actor ID 只处理首次出现的项目。
     * @param maxActorsPerCell 每格在本次调用中的最大 actor 数；零表示不分配任何 actor。
     * @param initialOccupancy 每格在本组规划开始前的占用数；不修改输入数据。
     * @return 按首次出现的 actor 顺序列出分配与未分配 actor。不同调用互不共享新分配的占用。
     */
    [[nodiscard]] PlanResult Plan(
        const std::vector<ActorCandidates>& actors,
        std::uint32_t maxActorsPerCell,
        const std::vector<CellOccupancy>& initialOccupancy = {});
}
