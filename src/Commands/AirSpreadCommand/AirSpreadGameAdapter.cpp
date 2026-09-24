#include "Commands/AirSpreadCommand/AirSpreadGameAdapter.h"

#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Game/CursorCellAccess.h"
#include "Game/GameObjectAccess.h"
#include "Game/SelectionAccess.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ra_commands::game
{
    namespace
    {
        // 限制一次热键的地图查询与快照大小；超出 32 格的单位保留为未分配。
        constexpr std::int32_t MAX_SEARCH_RADIUS_CELLS = 32;
        // YRpp 的 GetCellIndex 将 Y 左移 9 位，两个坐标均须处于 0..511。
        constexpr std::int32_t MAP_AXIS_LIMIT = 512;

        using Occupancy = std::map<std::uint32_t, std::uint32_t>;

        struct CandidateCell
        {
            air_spread::Cell Location;
            std::int32_t DistanceSquared = 0;
        };

        bool IsMapCoordinate(air_spread::Cell cell)
        {
            return cell.X >= 0 && cell.X < MAP_AXIS_LIMIT &&
                cell.Y >= 0 && cell.Y < MAP_AXIS_LIMIT;
        }

        bool IsValidMapCell(const MapClass* map, air_spread::Cell cell)
        {
            if (!map || !IsMapCoordinate(cell))
            {
                return false;
            }
            const CellStruct coords{
                static_cast<short>(cell.X), static_cast<short>(cell.Y) };
            // Cell 对象可能存在于不可用的地图边缘，单凭 TryGetCellAt 不足以作为移动目标。
            if (!map->CoordinatesLegal(coords) ||
                !map->IsWithinUsableArea(coords, false))
            {
                return false;
            }
            const auto* const gameCell = map->TryGetCellAt(coords);
            return gameCell && gameCell->MapCoords == coords;
        }

        std::uint32_t CellKey(air_spread::Cell cell)
        {
            return (static_cast<std::uint32_t>(cell.Y) << 9) |
                static_cast<std::uint32_t>(cell.X);
        }

        std::uint32_t CountAt(const Occupancy& occupancy, air_spread::Cell cell)
        {
            const auto entry = occupancy.find(CellKey(cell));
            return entry == occupancy.end() ? 0 : entry->second;
        }

        air_spread::ActorId EncodeActor(TechnoClass* techno)
        {
            static_assert(sizeof(void*) == sizeof(std::uint32_t));
            return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(techno)) << 32) |
                techno->UniqueID;
        }

        bool TryGetFlyingKind(TechnoClass* techno, air_spread::ActorKind& outKind)
        {
            if (!techno || techno->UniqueID == 0 || !techno->IsAlive ||
                !techno->IsOnMap || techno->InLimbo ||
                !techno->IsInPlayfield || techno->IsDead() || techno->Transporter)
            {
                return false;
            }

            const auto kind = techno->WhatAmI();
            if (kind == AbstractType::Aircraft)
            {
                outKind = air_spread::ActorKind::Aircraft;
                return true;
            }
            if (kind != AbstractType::Infantry && kind != AbstractType::Unit)
            {
                return false;
            }

            const auto* const type = techno->GetTechnoType();
            if (!type || (type->MovementZone != MovementZone::Fly &&
                type->SpeedType != SpeedType::Winged))
            {
                return false;
            }
            outKind = kind == AbstractType::Infantry
                ? air_spread::ActorKind::FlyingInfantry
                : air_spread::ActorKind::FlyingUnit;
            return true;
        }

        std::vector<CandidateCell> CollectOrderedCells(air_spread::Cell center)
        {
            std::vector<CandidateCell> cells;
            const auto lowX = (std::max)(0, center.X - MAX_SEARCH_RADIUS_CELLS);
            const auto highX = (std::min)(MAP_AXIS_LIMIT - 1,
                center.X + MAX_SEARCH_RADIUS_CELLS);
            const auto lowY = (std::max)(0, center.Y - MAX_SEARCH_RADIUS_CELLS);
            const auto highY = (std::min)(MAP_AXIS_LIMIT - 1,
                center.Y + MAX_SEARCH_RADIUS_CELLS);
            for (auto y = lowY; y <= highY; ++y)
            {
                for (auto x = lowX; x <= highX; ++x)
                {
                    const auto dx = x - center.X;
                    const auto dy = y - center.Y;
                    const auto distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared <= MAX_SEARCH_RADIUS_CELLS * MAX_SEARCH_RADIUS_CELLS)
                    {
                        cells.push_back({{x, y}, distanceSquared});
                    }
                }
            }
            std::sort(cells.begin(), cells.end(),
                [](const CandidateCell& left, const CandidateCell& right)
                {
                    if (left.DistanceSquared != right.DistanceSquared)
                    {
                        return left.DistanceSquared < right.DistanceSquared;
                    }
                    if (left.Location.Y != right.Location.Y)
                    {
                        return left.Location.Y < right.Location.Y;
                    }
                    return left.Location.X < right.Location.X;
                });
            return cells;
        }

        void AssignCandidates(
            air_spread::AirSpreadSnapshot& snapshot,
            const MapClass* map,
            air_spread::Cell center,
            const Occupancy& infantryOccupancy,
            const Occupancy& flyingOccupancy)
        {
            std::vector<air_spread::Cell> infantryCells;
            std::vector<air_spread::Cell> flyingCells;
            std::uint64_t infantrySlots = 0;
            std::uint64_t flyingSlots = 0;
            for (const auto& candidate : CollectOrderedCells(center))
            {
                if (!IsValidMapCell(map, candidate.Location))
                {
                    continue;
                }
                const auto infantryCount = CountAt(infantryOccupancy, candidate.Location);
                if (infantryCount < 3)
                {
                    infantryCells.push_back(candidate.Location);
                    infantrySlots += 3 - infantryCount;
                }
                if (CountAt(flyingOccupancy, candidate.Location) == 0)
                {
                    flyingCells.push_back(candidate.Location);
                    ++flyingSlots;
                }
                if (infantrySlots >= snapshot.InfantryActors.size() &&
                    flyingSlots >= snapshot.FlyingActors.size())
                {
                    break;
                }
            }

            for (auto& actor : snapshot.InfantryActors)
            {
                actor.Cells = infantryCells;
            }
            for (auto& actor : snapshot.FlyingActors)
            {
                actor.Cells = flyingCells;
            }
        }

        void CopyOccupancy(const Occupancy& source,
            std::vector<air_spread::CellOccupancy>& destination)
        {
            destination.reserve(source.size());
            for (const auto& [key, count] : source)
            {
                destination.push_back({
                    {static_cast<std::int32_t>(key & 511u),
                     static_cast<std::int32_t>(key >> 9)}, count});
            }
        }
    }

    AirSpreadGameAdapter::AirSpreadGameAdapter(
        commands::ClickedMissionDispatcher& dispatcher)
        : mDispatcher(dispatcher)
    {
    }

    bool AirSpreadGameAdapter::TryCaptureSnapshot(
        air_spread::AirSpreadSnapshot& outSnapshot) const
    {
        if (!IsGameSessionReady() || !mDispatcher.IsSessionActive())
        {
            return false;
        }
        const auto cursor = CaptureCursorCell();
        if (!cursor)
        {
            return false;
        }
        const air_spread::Cell center{cursor->X, cursor->Y};
        const auto* const map = MapClass::Instance.get();
        if (!IsValidMapCell(map, center))
        {
            return false;
        }

        air_spread::AirSpreadSnapshot snapshot;
        Occupancy infantryOccupancy;
        Occupancy flyingOccupancy;
        std::unordered_set<air_spread::ActorId> seenActors;
        const auto selectedIds = CaptureSelectedUnitIds();
        const std::unordered_set<air_spread::ActorId> selectedActors(
            selectedIds.begin(), selectedIds.end());
        auto* const technos = TechnoClass::Array.get();
        auto* const local = HouseClass::Player.get();
        snapshot.InfantryActors.reserve(selectedIds.size());
        snapshot.FlyingActors.reserve(selectedIds.size());
        seenActors.reserve(technos->Count);
        for (int index = 0; index < technos->Count; ++index)
        {
            auto* const techno = technos->Items[index];
            air_spread::ActorKind kind;
            if (!TryGetFlyingKind(techno, kind))
            {
                continue;
            }
            const auto id = EncodeActor(techno);
            if (!seenActors.insert(id).second)
            {
                continue;
            }

            // 规划前先排除本次会移动的选中者，避免把自身位置当作已有占用。
            if (techno->Owner == local && selectedActors.contains(id))
            {
                auto& actors = kind == air_spread::ActorKind::FlyingInfantry
                    ? snapshot.InfantryActors : snapshot.FlyingActors;
                actors.push_back({id, kind, {}});
                continue;
            }

            const auto gameCell = techno->GetMapCoords();
            const air_spread::Cell location{gameCell.X, gameCell.Y};
            if (!IsValidMapCell(map, location))
            {
                continue;
            }
            auto& occupancy = kind == air_spread::ActorKind::FlyingInfantry
                ? infantryOccupancy : flyingOccupancy;
            auto& count = occupancy[CellKey(location)];
            if (count < (std::numeric_limits<std::uint32_t>::max)())
            {
                ++count;
            }
        }

        AssignCandidates(snapshot, map, center, infantryOccupancy, flyingOccupancy);
        CopyOccupancy(infantryOccupancy, snapshot.InfantryOccupancy);
        CopyOccupancy(flyingOccupancy, snapshot.FlyingOccupancy);
        outSnapshot = std::move(snapshot);
        return true;
    }

    bool AirSpreadGameAdapter::SubmitMove(
        air_spread::ActorId actor, air_spread::Cell destination)
    {
        if (!mDispatcher.IsSessionActive() || !IsGameSessionReady() ||
            !IsValidMapCell(MapClass::Instance.get(), destination))
        {
            return false;
        }
        const auto uniqueId = static_cast<std::uint32_t>(actor);
        const auto address = static_cast<std::uintptr_t>(actor >> 32);
        auto* const techno = FindLiveTechno(uniqueId);
        air_spread::ActorKind kind;
        const auto selectedIds = CaptureSelectedUnitIds();
        if (!techno || reinterpret_cast<std::uintptr_t>(techno) != address ||
            techno->Owner != HouseClass::Player.get() ||
            std::find(selectedIds.begin(), selectedIds.end(), actor) == selectedIds.end() ||
            !TryGetFlyingKind(techno, kind))
        {
            return false;
        }

        const auto epoch = mDispatcher.Epoch();
        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(techno, epoch);
        intent.Producer = commands::ClickedMissionProducer::AirSpread;
        intent.Mission = static_cast<std::int32_t>(Mission::Move);
        intent.DestinationCell = commands::CellCoordinate{destination.X, destination.Y};
        intent.Epoch = epoch;
        intent.CreatedFrame = GetCurrentGameFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        const auto result = mDispatcher.Submit(intent);
        return result == commands::ClickedMissionEnqueueResult::Enqueued ||
            result == commands::ClickedMissionEnqueueResult::Duplicate ||
            result == commands::ClickedMissionEnqueueResult::Replaced;
    }
}
