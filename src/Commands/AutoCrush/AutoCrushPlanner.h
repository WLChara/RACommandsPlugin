#pragma once

#include <cstdint>
#include <vector>

namespace ra_commands::auto_crush
{
    using CrusherId = std::uint64_t;
    using TargetId = std::uint64_t;
    constexpr int LOCAL_RADIUS_CELLS = 8;
    // 第 8 格的步兵需要其后方第 9 格作为 Move 终点。
    constexpr int MAX_ROUTE_CELLS = LOCAL_RADIUS_CELLS + 1;

    struct Cell
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;

        bool operator==(const Cell&) const = default;
    };

    // x 向东、y 向南；枚举值按顺时针排列，适配器负责转换游戏朝向。
    enum class Facing : std::uint8_t
    {
        North,
        NorthEast,
        East,
        SouthEast,
        South,
        SouthWest,
        West,
        NorthWest
    };

    struct TargetPrediction
    {
        TargetId mId = 0;
        Cell mCurrentCell;
        Cell mNextCell;
        // 从快照时刻到进入下一格的预计步数；0 表示立即转移。
        std::uint32_t mTransitionSteps = 0;
        // 转移后位于下一格的概率，剩余概率仍留在当前格；范围为 0..1。
        double mConfidence = 1.0;
    };

    struct CrusherSnapshot
    {
        CrusherId mId = 0;
        Cell mCurrentCell;
        Facing mFacing = Facing::North;
        // 适配器放入八方向最多 MAX_ROUTE_CELLS 格的可通行格，目标仍限前 8 格。
        std::vector<Cell> mTraversableCells;
        // 可通行但不能作为 Move 终点的格；不在此集合的可通行格可停靠。
        std::vector<Cell> mNonStoppableCells;
        // 适配器仅放入本车可以碾压的敌方步兵；同一 ID 最多贡献一次收益。
        std::vector<TargetPrediction> mTargets;
    };

    struct Snapshot
    {
        std::vector<CrusherSnapshot> mCrushers;
    };

    struct PlannedMove
    {
        CrusherId mCrusherId = 0;
        Cell mDestination;
        // 从当前格的下一格开始，依次包含所有中间格与最终停靠格。
        std::vector<Cell> mPath;
        // 按 ID 升序排列；只包含至少有一条中间格命中可能性的目标。
        std::vector<TargetId> mExpectedTargetIds;
        // 每目标取路径上的最大命中概率，再对不同目标求和。
        double mExpectedCrushCount = 0.0;
        // 预期数量 /（路径步数 + 每次 45 度初始转向的半步惩罚）。
        double mScore = 0.0;
    };

    struct PlanResult
    {
        std::vector<PlannedMove> mMoves;
    };

    /**
     * 在每车 8 格目标半径内规划直线 Move，终点最多在第 9 格；只输出正收益路线。
     * 重复车辆 ID 只使用首次快照，输出按车辆 ID 升序排列。
     * 纯路径只表示规划意图，不保证游戏原生寻路实际沿线行驶。
     */
    [[nodiscard]] PlanResult Plan(const Snapshot& snapshot);
}
