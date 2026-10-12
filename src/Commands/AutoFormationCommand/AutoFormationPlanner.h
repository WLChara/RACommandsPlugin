#pragma once

#include <cstdint>
#include <vector>

namespace ra_commands::auto_formation
{
    using ActorId = std::uint64_t;

    struct Cell
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;
        bool mOnBridge = false;

        bool operator==(const Cell&) const = default;
    };

    struct Center
    {
        double mX = 0.0;
        double mY = 0.0;
    };

    enum class ActorKind { Vehicle, Infantry };
    enum class OccupancyPlane { Ground, Air };

    struct ActorProfile
    {
        ActorId mId = 0;
        ActorKind mKind = ActorKind::Vehicle;
        OccupancyPlane mPlane = OccupancyPlane::Ground;
        bool mOnBridge = false;
        bool operator==(const ActorProfile&) const = default;
    };

    struct ActorObservation
    {
        Cell mCell;
        ActorKind mKind = ActorKind::Vehicle;
        OccupancyPlane mPlane = OccupancyPlane::Ground;
    };

    struct CandidateCapacity
    {
        Cell mCell;
        std::uint8_t mAvailable = 0;
    };

    struct ActorSnapshot
    {
        ActorId mId = 0;
        Cell mCurrentCell;
        // 格中心为整数；适配器将游戏坐标换算为连续格坐标。
        Center mPosition;
        // 只含适配器确认可占的候选格；不能将这个集合当作原生寻路成功证明。
        std::vector<Cell> mCandidates;
        ActorKind mKind = ActorKind::Vehicle;
        OccupancyPlane mPlane = OccupancyPlane::Ground;
        // 未列出的候选具有本类别完整容量（载具1、步兵3）；部分占用必须明确列出。
        std::vector<CandidateCapacity> mCapacityLimits;
    };

    struct Snapshot
    {
        Center mCenter;
        std::vector<ActorSnapshot> mActors;
    };

    struct Assignment
    {
        ActorId mActor = 0;
        Cell mDestination;
        ActorKind mKind = ActorKind::Vehicle;
        OccupancyPlane mPlane = OccupancyPlane::Ground;
        // 逻辑预约号，不是游戏原生子位置编号；步兵0..2，载具仅0。
        std::uint8_t mSeat = 0;

        bool operator==(const Assignment&) const = default;
    };

    struct PlanResult
    {
        Center mCenter;
        std::vector<Assignment> mAssignments;
        std::vector<ActorId> mUnassignedActors;
        // 超预算整次拒绝，不能把截断后的部分队伍当成完整列队。
        bool mBudgetExceeded = false;
    };

    /** 计算首次出现的有效 actor 的平均位置；空集合返回零中心。 */
    [[nodiscard]] Center CalculateCenter(const std::vector<ActorSnapshot>& actors);

    /**
     * 对值快照作有界匹配；陆空资源独立，同层同格为一辆车或最多三名步兵。
     * 优先分配数量、紧凑且近方形的包络和内部填充，最后减少移动。
     * 相同输入产生相同结果；不持有游戏对象或调用游戏 API。
     */
    [[nodiscard]] PlanResult Plan(const Snapshot& snapshot);
}
