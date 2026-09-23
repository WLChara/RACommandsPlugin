#include "Commands/TeslaChargeCommand/TeslaChargePlanner.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ra_commands::tesla_charge
{
    namespace
    {
        constexpr std::int64_t MAX_CELL_DISTANCE_SQUARED = 32 * 32;

        bool IsLocal(const ObjectSnapshot& object, std::uintptr_t localOwner)
        {
            return object.Id != 0 && localOwner != 0 && object.Owner == localOwner;
        }

        std::int64_t DistanceSquared(
            const ObjectSnapshot& left,
            const ObjectSnapshot& right)
        {
            const auto dx = static_cast<std::int64_t>(left.CellX) - right.CellX;
            const auto dy = static_cast<std::int64_t>(left.CellY) - right.CellY;
            return dx * dx + dy * dy;
        }

        bool IsAssigned(const std::vector<Assignment>& assignments, UnitId charger)
        {
            return std::any_of(assignments.begin(), assignments.end(),
                [charger](const Assignment& assignment)
                {
                    return assignment.Charger == charger;
                });
        }
    }

    std::vector<Assignment> Plan(
        const Snapshot& snapshot,
        const std::vector<Assignment>& previous)
    {
        std::vector<Assignment> result;
        for (const auto& tesla : snapshot.Teslas)
        {
            if (IsLocal(tesla, snapshot.LocalOwner))
            {
                result.push_back({tesla.Id, 0});
            }
        }

        // 先占住仍有效的旧配对，避免每次维护时频繁切换目标。
        for (const auto& old : previous)
        {
            auto slot = std::find_if(result.begin(), result.end(),
                [&old](const Assignment& assignment)
                {
                    return assignment.Tesla == old.Tesla && assignment.Charger == 0;
                });
            if (slot == result.end() || IsAssigned(result, old.Charger))
            {
                continue;
            }

            const auto tesla = std::find_if(snapshot.Teslas.begin(), snapshot.Teslas.end(),
                [&old](const ObjectSnapshot& object) { return object.Id == old.Tesla; });
            const auto charger = std::find_if(snapshot.Chargers.begin(), snapshot.Chargers.end(),
                [&old](const ObjectSnapshot& object) { return object.Id == old.Charger; });
            if (tesla != snapshot.Teslas.end() && charger != snapshot.Chargers.end() &&
                IsLocal(*charger, snapshot.LocalOwner) &&
                charger->Owner == tesla->Owner &&
                DistanceSquared(*tesla, *charger) <= MAX_CELL_DISTANCE_SQUARED)
            {
                slot->Charger = charger->Id;
            }
        }

        for (auto& slot : result)
        {
            if (slot.Charger != 0)
            {
                continue;
            }

            const auto tesla = std::find_if(snapshot.Teslas.begin(), snapshot.Teslas.end(),
                [&slot](const ObjectSnapshot& object) { return object.Id == slot.Tesla; });
            if (tesla == snapshot.Teslas.end())
            {
                continue;
            }

            std::int64_t bestDistance = (std::numeric_limits<std::int64_t>::max)();
            for (const auto& charger : snapshot.Chargers)
            {
                if (!IsLocal(charger, snapshot.LocalOwner) ||
                    charger.Owner != tesla->Owner || IsAssigned(result, charger.Id))
                {
                    continue;
                }

                const auto distance = DistanceSquared(*tesla, charger);
                if (distance <= MAX_CELL_DISTANCE_SQUARED && distance < bestDistance)
                {
                    bestDistance = distance;
                    slot.Charger = charger.Id;
                }
            }
        }

        result.erase(std::remove_if(result.begin(), result.end(),
            [](const Assignment& assignment) { return assignment.Charger == 0; }), result.end());
        return result;
    }
}
