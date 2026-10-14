#pragma once
#include <cstddef>

namespace ra_commands::auto_formation
{
    inline constexpr std::size_t MAX_FORMATION_ACTORS = 1024;
    inline constexpr std::size_t LEGACY_FORMATION_ACTORS = 256;
    inline constexpr std::size_t LEGACY_DRAG_ACTORS = 128;
    inline constexpr std::size_t LEGACY_PLACEMENT_CELLS = 1024;
    inline constexpr std::size_t MAX_FORMATION_PLACEMENT_CELLS = 4096;
    inline constexpr std::size_t MAX_FORMATION_CANDIDATES_PER_ACTOR = 4096;
    inline constexpr std::size_t MAX_FORMATION_RAW_EDGES =
        MAX_FORMATION_ACTORS * MAX_FORMATION_CANDIDATES_PER_ACTOR;
    // 地面、桥面与空中是三个独立资源平面。
    inline constexpr std::size_t MAX_FORMATION_RESOURCE_CELLS = 3 * MAX_FORMATION_PLACEMENT_CELLS;
    inline constexpr std::size_t MAX_LARGE_FORMATION_FLOW_EDGES = 2000000;
    inline constexpr std::size_t MAX_LARGE_FORMATION_WORK = 80000000;
}
