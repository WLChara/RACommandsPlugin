#include "Commands/AirSpreadCommand/AirSpreadPlanner.h"

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace ra_commands::air_spread
{
    namespace
    {
        struct CellHash
        {
            std::size_t operator()(const Cell& cell) const noexcept
            {
                const auto xHash = std::hash<std::int32_t>{}(cell.X);
                const auto yHash = std::hash<std::int32_t>{}(cell.Y);
                return xHash ^ (yHash + static_cast<std::size_t>(0x9e3779b9) +
                    (xHash << 6) + (xHash >> 2));
            }
        };
    }

    PlanResult Plan(
        const std::vector<ActorCandidates>& actors,
        std::uint32_t maxActorsPerCell,
        const std::vector<CellOccupancy>& initialOccupancy)
    {
        PlanResult result;
        result.Assignments.reserve(actors.size());
        result.UnassignedActors.reserve(actors.size());

        std::unordered_set<ActorId> seenActors;
        seenActors.reserve(actors.size());
        std::unordered_map<Cell, std::uint32_t, CellHash> occupancy;
        occupancy.reserve(actors.size() + initialOccupancy.size());

        for (const auto& cellOccupancy : initialOccupancy)
        {
            auto& count = occupancy[cellOccupancy.Location];
            if (cellOccupancy.Count >= maxActorsPerCell - count)
            {
                count = maxActorsPerCell;
            }
            else
            {
                count += cellOccupancy.Count;
            }
        }

        for (const auto& actor : actors)
        {
            if (!seenActors.insert(actor.Id).second)
            {
                continue;
            }

            bool assigned = false;
            if (maxActorsPerCell > 0)
            {
                for (const auto& cell : actor.Cells)
                {
                    auto& count = occupancy[cell];
                    if (count >= maxActorsPerCell)
                    {
                        continue;
                    }

                    ++count;
                    result.Assignments.push_back({actor.Id, actor.Kind, cell});
                    assigned = true;
                    break;
                }
            }

            if (!assigned)
            {
                result.UnassignedActors.push_back(actor.Id);
            }
        }

        return result;
    }
}
