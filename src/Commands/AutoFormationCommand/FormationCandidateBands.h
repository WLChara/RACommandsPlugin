#pragma once
#include "Commands/AutoFormationCommand/LargeFormationMatching.h"
#include <cstddef>
#include <vector>

namespace ra_commands::auto_formation
{
    inline constexpr std::size_t LARGE_FORMATION_ACCEPTED_CANDIDATES = 64;
    inline constexpr std::size_t LARGE_FORMATION_QUERIES_PER_ACTOR = 256;
    struct FormationCandidateBand
    {
        ActorId mActor;
        std::vector<std::size_t> mCellIndices;
    };
    /** 在理想槽周围最多16格收集近邻，返回显式 cells 的索引。
     * 同一个步兵 bundle 使用相同锚点和查询顺序；真正可占性须由调用方逐演员核对。
     * 复杂地形下局部图可能不完整；不能把失败包装成全场完整候选查询。
     */
    [[nodiscard]] std::vector<FormationCandidateBand> BuildFormationCandidateBands(
        const Snapshot& snapshot, const std::vector<Cell>& cells,
        const std::vector<PlaneLayout>& planes = {}, std::uint8_t infantryCapacity = 3);
}
