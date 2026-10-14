#include "Commands/AutoFormationCommand/FormationCandidateBands.h"
#include "Commands/AutoFormationCommand/FormationLimits.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <unordered_map>

namespace ra_commands::auto_formation
{
    namespace
    {
        constexpr int MAP_AXIS = 512;
        constexpr int CANDIDATE_RADIUS = 16;
        std::uint32_t Key(int x, int y) { return static_cast<std::uint32_t>(x | (y << 9)); }
    }
    std::vector<FormationCandidateBand> BuildFormationCandidateBands(const Snapshot& snapshot,
        const std::vector<Cell>& cells, const std::vector<PlaneLayout>& planes,
        std::uint8_t infantryCapacity)
    {
        if (cells.empty() || cells.size() > MAX_FORMATION_RESOURCE_CELLS) return {};
        const auto anchors = BuildFormationCandidateAnchors(snapshot, planes, infantryCapacity);
        if (anchors.size() != snapshot.mActors.size()) return {};
        std::unordered_map<std::uint32_t, std::size_t> lookup;
        for (std::size_t i = 0; i < cells.size(); ++i)
        {
            const auto cell = cells[i];
            if (cell.mX < 0 || cell.mX >= MAP_AXIS || cell.mY < 0 || cell.mY >= MAP_AXIS) return {};
            const auto [it, inserted] = lookup.emplace(Key(cell.mX, cell.mY), i);
            if (!inserted) return {}; // 显式 XY 查询应已去重，避免重复消耗原生调用预算。
            (void)it;
        }
        std::map<std::pair<double, double>, std::vector<std::size_t>> shared;
        std::vector<FormationCandidateBand> bands;
        bands.reserve(anchors.size());
        for (const auto& anchor : anchors)
        {
            const auto center = anchor.mIdealCenter;
            if (center.mX < -32 || center.mX > 543 || center.mY < -32 || center.mY > 543) return {};
            const auto key = std::pair{center.mX, center.mY};
            auto found = shared.find(key);
            if (found == shared.end())
            {
                std::vector<std::pair<double, std::size_t>> nearest;
                const int centerX = static_cast<int>(std::round(center.mX));
                const int centerY = static_cast<int>(std::round(center.mY));
                for (int x = std::max(0, centerX - CANDIDATE_RADIUS); x <= std::min(MAP_AXIS - 1, centerX + CANDIDATE_RADIUS); ++x)
                    for (int y = std::max(0, centerY - CANDIDATE_RADIUS); y <= std::min(MAP_AXIS - 1, centerY + CANDIDATE_RADIUS); ++y)
                    {
                        const auto cell = lookup.find(Key(x, y));
                        if (cell == lookup.end()) continue;
                        const double dx = x - center.mX, dy = y - center.mY;
                        nearest.push_back({dx * dx + dy * dy, cell->second});
                    }
                const auto compare = [&](const auto& a, const auto& b)
                { return std::tuple{a.first, cells[a.second].mY, cells[a.second].mX}
                    < std::tuple{b.first, cells[b.second].mY, cells[b.second].mX}; };
                const auto count = std::min(LARGE_FORMATION_QUERIES_PER_ACTOR, nearest.size());
                std::partial_sort(nearest.begin(), nearest.begin() + count, nearest.end(), compare);
                std::vector<std::size_t> indices;
                for (std::size_t i = 0; i < count; ++i) indices.push_back(nearest[i].second);
                found = shared.emplace(key, std::move(indices)).first;
            }
            if (found->second.empty()) return {};
            bands.push_back({anchor.mActor, found->second});
        }
        return bands;
    }
}
