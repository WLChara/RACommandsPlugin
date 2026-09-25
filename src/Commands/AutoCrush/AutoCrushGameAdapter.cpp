#include "Commands/AutoCrush/AutoCrushGameAdapter.h"

#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <CellClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <MapClass.h>
#include <ObjectClass.h>
#include <UnitClass.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ra_commands::game
{
    namespace
    {
        constexpr int MAP_AXIS_LIMIT = 512;
        constexpr std::size_t MAX_CRUSHERS_PER_SNAPSHOT = 128;
        constexpr std::size_t MAX_TARGETS_PER_CRUSHER = 256;
        constexpr int MAX_CELL_CONTENT = 32;
        constexpr std::array<auto_crush::Cell, 8> ROUTE_DIRECTIONS{{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
        }};

        auto_crush::CrusherId EncodeId(const TechnoClass* techno)
        {
            static_assert(sizeof(void*) == sizeof(std::uint32_t));
            return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(techno)) << 32) |
                techno->UniqueID;
        }

        bool IsEligibleUnit(const UnitClass* unit)
        {
            return unit && unit->UniqueID != 0 &&
                unit->Owner == HouseClass::Player.get() && unit->Type &&
                (unit->Type->Crusher || unit->Type->OmniCrusher) &&
                unit->Type->MovementZone != MovementZone::Fly &&
                unit->Type->SpeedType != SpeedType::Winged &&
                unit->IsAlive && unit->IsOnMap && !unit->InLimbo &&
                unit->IsInPlayfield && !unit->IsDead() &&
                !unit->Transporter && !unit->InAir && !unit->OnBridge &&
                !unit->Deactivated && !unit->IsImmobilized &&
                unit->InWhichLayer() == Layer::Ground;
        }

        UnitClass* ResolveCrusher(auto_crush::CrusherId id)
        {
            if (!IsGameSessionReady())
            {
                return nullptr;
            }
            const auto uniqueId = static_cast<std::uint32_t>(id);
            const auto address = static_cast<std::uintptr_t>(id >> 32);
            auto* const units = UnitClass::Array.get();
            for (int index = 0; index < units->Count; ++index)
            {
                auto* const unit = units->Items[index];
                if (unit && unit->UniqueID == uniqueId &&
                    reinterpret_cast<std::uintptr_t>(unit) == address)
                {
                    return IsEligibleUnit(unit) ? unit : nullptr;
                }
            }
            return nullptr;
        }

        CellClass* FindMapCell(const MapClass* map, auto_crush::Cell cell)
        {
            if (!map || cell.mX < 0 || cell.mX >= MAP_AXIS_LIMIT ||
                cell.mY < 0 || cell.mY >= MAP_AXIS_LIMIT)
            {
                return nullptr;
            }
            const CellStruct coords{
                static_cast<short>(cell.mX), static_cast<short>(cell.mY)};
            if (!map->CoordinatesLegal(coords) ||
                !map->IsWithinUsableArea(coords, false))
            {
                return nullptr;
            }
            auto* const found = map->TryGetCellAt(coords);
            return found && found->MapCoords == coords ? found : nullptr;
        }

        bool IsKnownOpenTerrain(const CellClass* cell, const CellClass* origin,
            bool allowOwnUnit = false)
        {
            // 缺少精确且无副作用的逐车通行查询，只把已见的平坦空地和道路纳入规划。
            // SDK 标注 Foggedness == -1 为可见；对象的 IsVisible 仅表示上次是否绘入视口。
            return cell && origin && !cell->IsShrouded() && cell->Foggedness == -1 &&
                !cell->ContainsBridge() && !cell->AltObject &&
                cell->SlopeIndex == 0 && cell->Level == origin->Level &&
                cell->OverlayTypeIndex < 0 &&
                (allowOwnUnit || cell->AltOccupationFlags == 0) &&
                (cell->LandType == LandType::Clear || cell->LandType == LandType::Road);
        }

        bool CanCrushVisibleInfantry(const UnitClass* crusher,
            const InfantryClass* infantry, auto_crush::Cell cell)
        {
            // 无准确的无副作用碾压判定接口，OmniCrusher 也只采纳可确认的共同子集。
            if (!infantry || !infantry->Type || infantry->UniqueID == 0 ||
                !infantry->Owner ||
                HouseClass::Player->IsAlliedWith(infantry->Owner) ||
                !infantry->IsAlive || !infantry->IsOnMap || infantry->InLimbo ||
                !infantry->IsInPlayfield || infantry->IsDead() ||
                infantry->Transporter || infantry->InAir || infantry->OnBridge ||
                infantry->InWhichLayer() != Layer::Ground ||
                infantry->CloakState != CloakState::Uncloaked ||
                infantry->Disguised || infantry->Uncrushable ||
                !infantry->Type->Crushable || infantry->Type->OmniCrushResistant)
            {
                return false;
            }
            const auto coords = infantry->GetMapCoords();
            return coords.X == cell.mX && coords.Y == cell.mY &&
                crusher->Owner != infantry->Owner;
        }

        bool TryClassifyCell(const MapClass* map, const CellClass* origin,
            const UnitClass* crusher, auto_crush::Cell position,
            std::vector<auto_crush::TargetPrediction>& outTargets)
        {
            outTargets.clear();
            auto* const cell = FindMapCell(map, position);
            if (!IsKnownOpenTerrain(cell, origin))
            {
                return false;
            }
            if (cell->GetUnit(false) || cell->GetBuilding())
            {
                return false;
            }
            auto* object = cell->FirstObject;
            int count = 0;
            while (object && count < MAX_CELL_CONTENT)
            {
                if (object->WhatAmI() != AbstractType::Infantry)
                {
                    return false;
                }
                auto* const infantry = static_cast<InfantryClass*>(object);
                // 任意一名不可确认能碾压的占用者都会封锁整格，不发送混合占用格。
                if (!CanCrushVisibleInfantry(crusher, infantry, position))
                {
                    return false;
                }
                // Destination 不是可确认的下一格；只向规划器提供当前格的静止预测。
                outTargets.push_back({EncodeId(infantry), position, position, 0, 1.0});
                object = object->NextObject;
                ++count;
            }
            return object == nullptr;
        }

        bool CanAssessRoute(const UnitClass* crusher, const CellClass* origin)
        {
            return crusher && crusher->Type && origin &&
                IsKnownOpenTerrain(origin, origin, true);
        }

        commands::ClickedMissionIdentity IdentityFromId(
            auto_crush::CrusherId id, std::uint32_t epoch)
        {
            return {static_cast<std::uintptr_t>(id >> 32),
                static_cast<std::uint32_t>(id),
                static_cast<std::uint32_t>(AbstractType::Unit), epoch};
        }

        bool IsAutoCrushMoveShape(const commands::ClickedMissionIntent& intent)
        {
            return intent.Producer == commands::ClickedMissionProducer::AutoCrush &&
                intent.Mission == static_cast<std::int32_t>(Mission::Move) &&
                intent.Actor.Kind == static_cast<std::uint32_t>(AbstractType::Unit) &&
                intent.Actor.Epoch == intent.Epoch && intent.DestinationCell &&
                !intent.Target && !intent.TargetCell && !intent.Nearest;
        }
    }

    CellClass* ResolveAutoCrushMoveCell(
        const commands::ClickedMissionIntent& intent, TechnoClass*& outActor)
    {
        outActor = nullptr;
        if (!IsAutoCrushMoveShape(intent))
        {
            return nullptr;
        }
        auto* const techno = ResolveIdentity(intent.Actor);
        const auto id = (static_cast<std::uint64_t>(intent.Actor.Address) << 32) |
            intent.Actor.UniqueId;
        auto* const crusher = ResolveCrusher(id);
        if (!techno || techno != crusher)
        {
            return nullptr;
        }

        auto* const map = MapClass::Instance.get();
        const auto source = crusher->GetMapCoords();
        const auto_crush::Cell current{source.X, source.Y};
        auto* const origin = FindMapCell(map, current);
        if (!CanAssessRoute(crusher, origin))
        {
            return nullptr;
        }

        const auto destination = *intent.DestinationCell;
        auto* const destinationCell = FindMapCell(map, {destination.X, destination.Y});
        if (!destinationCell)
        {
            return nullptr;
        }
        const int deltaX = destination.X - current.mX;
        const int deltaY = destination.Y - current.mY;
        const int steps = (std::max)(std::abs(deltaX), std::abs(deltaY));
        if (steps < 2 || steps > auto_crush::MAX_ROUTE_CELLS ||
            (deltaX != 0 && deltaY != 0 && std::abs(deltaX) != std::abs(deltaY)))
        {
            return nullptr;
        }

        const int stepX = (deltaX > 0) - (deltaX < 0);
        const int stepY = (deltaY > 0) - (deltaY < 0);
        bool hasTarget = false;
        std::vector<auto_crush::TargetPrediction> targets;
        for (int step = 1; step <= steps; ++step)
        {
            const auto_crush::Cell position{
                current.mX + stepX * step, current.mY + stepY * step};
            if (!TryClassifyCell(map, origin, crusher, position, targets))
            {
                return nullptr;
            }
            if (step == steps)
            {
                if (!targets.empty())
                {
                    return nullptr;
                }
            }
            else
            {
                hasTarget |= !targets.empty();
            }
        }
        if (!hasTarget)
        {
            return nullptr;
        }

        outActor = crusher;
        return destinationCell;
    }

    AutoCrushGameAdapter::AutoCrushGameAdapter(
        commands::ClickedMissionDispatcher& dispatcher)
        : mDispatcher(dispatcher)
    {
    }

    bool AutoCrushGameAdapter::IsSessionActive() const
    {
        return mDispatcher.IsSessionActive() && IsGameSessionReady();
    }

    std::uint32_t AutoCrushGameAdapter::Epoch() const
    {
        return mDispatcher.Epoch();
    }

    std::uint32_t AutoCrushGameAdapter::CurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    std::vector<auto_crush::CrusherId>
        AutoCrushGameAdapter::CaptureSelectedVehicleIds() const
    {
        std::vector<auto_crush::CrusherId> ids;
        if (!IsSessionActive())
        {
            return ids;
        }
        auto* const units = UnitClass::Array.get();
        ids.reserve((std::min)(static_cast<std::size_t>(units->Count),
            MAX_CRUSHERS_PER_SNAPSHOT));
        for (int index = 0; index < units->Count; ++index)
        {
            auto* const unit = units->Items[index];
            if (unit && unit->IsSelected && unit->Owner == HouseClass::Player.get() &&
                unit->UniqueID != 0)
            {
                ids.push_back(EncodeId(unit));
            }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    std::vector<auto_crush::CrusherId>
        AutoCrushGameAdapter::CaptureSelectedEligibleCrushers() const
    {
        auto ids = CaptureSelectedVehicleIds();
        ids.erase(std::remove_if(ids.begin(), ids.end(), [this](auto_crush::CrusherId id)
        {
            return !IsEligibleCrusher(id);
        }), ids.end());
        return ids;
    }

    bool AutoCrushGameAdapter::IsEligibleCrusher(auto_crush::CrusherId id) const
    {
        if (!IsSessionActive())
        {
            mHasEligibleIds = false;
            return false;
        }
        RefreshEligibleIds();
        return mEligibleIds.contains(id);
    }

    void AutoCrushGameAdapter::RefreshEligibleIds() const
    {
        const auto frame = GetCurrentGameFrame();
        const auto epoch = mDispatcher.Epoch();
        if (mHasEligibleIds && mEligibleFrame == frame && mEligibleEpoch == epoch)
        {
            return;
        }
        mEligibleIds.clear();
        auto* const units = UnitClass::Array.get();
        for (int index = 0; index < units->Count; ++index)
        {
            auto* const unit = units->Items[index];
            if (IsEligibleUnit(unit))
            {
                mEligibleIds.insert(EncodeId(unit));
            }
        }
        mEligibleFrame = frame;
        mEligibleEpoch = epoch;
        mHasEligibleIds = true;
    }

    bool AutoCrushGameAdapter::TryCaptureSnapshot(
        const std::vector<auto_crush::CrusherId>& ids,
        auto_crush::Snapshot& outSnapshot) const
    {
        if (!IsSessionActive() || ids.size() > MAX_CRUSHERS_PER_SNAPSHOT)
        {
            return false;
        }
        auto* const map = MapClass::Instance.get();
        if (!map)
        {
            return false;
        }

        auto_crush::Snapshot snapshot;
        snapshot.mCrushers.reserve(ids.size());
        const std::unordered_set<auto_crush::CrusherId> requested(ids.begin(), ids.end());
        std::unordered_map<auto_crush::CrusherId, UnitClass*> eligible;
        eligible.reserve(requested.size());
        auto* const units = UnitClass::Array.get();
        for (int index = 0; index < units->Count; ++index)
        {
            auto* const unit = units->Items[index];
            if (unit && unit->UniqueID != 0)
            {
                const auto id = EncodeId(unit);
                if (requested.contains(id) && IsEligibleUnit(unit))
                {
                    eligible.emplace(id, unit);
                }
            }
        }
        for (const auto id : ids)
        {
            const auto found = eligible.find(id);
            if (found == eligible.end())
            {
                continue;
            }
            auto* const crusher = found->second;
            const auto coords = crusher->GetMapCoords();
            const auto_crush::Cell current{coords.X, coords.Y};
            auto* const origin = FindMapCell(map, current);
            if (!CanAssessRoute(crusher, origin))
            {
                continue;
            }

            auto_crush::CrusherSnapshot vehicle;
            vehicle.mId = id;
            vehicle.mCurrentCell = current;
            vehicle.mFacing = static_cast<auto_crush::Facing>(
                crusher->GetRealFacing().value8());
            vehicle.mTraversableCells.reserve(
                ROUTE_DIRECTIONS.size() * auto_crush::MAX_ROUTE_CELLS);
            std::vector<auto_crush::TargetPrediction> targets;
            bool overLimit = false;
            for (const auto direction : ROUTE_DIRECTIONS)
            {
                for (int step = 1; step <= auto_crush::MAX_ROUTE_CELLS; ++step)
                {
                    const auto_crush::Cell position{
                        current.mX + direction.mX * step,
                        current.mY + direction.mY * step};
                    if (!TryClassifyCell(map, origin, crusher, position, targets))
                    {
                        break;
                    }
                    if (step > auto_crush::LOCAL_RADIUS_CELLS)
                    {
                        // 第 9 格只允许空地落脚，不能扩大寻敌半径。
                        if (!targets.empty())
                        {
                            break;
                        }
                    }
                    if (targets.size() > MAX_TARGETS_PER_CRUSHER - vehicle.mTargets.size())
                    {
                        overLimit = true;
                        break;
                    }
                    vehicle.mTraversableCells.push_back(position);
                    if (!targets.empty())
                    {
                        vehicle.mNonStoppableCells.push_back(position);
                        vehicle.mTargets.insert(vehicle.mTargets.end(),
                            targets.begin(), targets.end());
                    }
                }
                if (overLimit)
                {
                    break;
                }
            }
            if (!overLimit)
            {
                snapshot.mCrushers.push_back(std::move(vehicle));
            }
        }
        outSnapshot = std::move(snapshot);
        return true;
    }

    bool AutoCrushGameAdapter::SubmitMove(
        auto_crush::CrusherId id, auto_crush::Cell destination)
    {
        if (!IsSessionActive())
        {
            return false;
        }
        const auto epoch = mDispatcher.Epoch();
        commands::ClickedMissionIntent intent;
        intent.Actor = IdentityFromId(id, epoch);
        intent.Producer = commands::ClickedMissionProducer::AutoCrush;
        intent.Supersession =
            commands::ClickedMissionSupersession::ReplaceSameProducerActorMission;
        intent.Mission = static_cast<std::int32_t>(Mission::Move);
        intent.DestinationCell = commands::CellCoordinate{
            destination.mX, destination.mY};
        intent.Epoch = epoch;
        intent.CreatedFrame = GetCurrentGameFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        TechnoClass* actor = nullptr;
        if (!ResolveAutoCrushMoveCell(intent, actor))
        {
            return false;
        }
        const auto result = mDispatcher.Submit(intent);
        return result == commands::ClickedMissionEnqueueResult::Enqueued ||
            result == commands::ClickedMissionEnqueueResult::Duplicate ||
            result == commands::ClickedMissionEnqueueResult::Replaced;
    }

    void AutoCrushGameAdapter::CancelPending(auto_crush::CrusherId id)
    {
        (void)mDispatcher.CancelByProducerAndActor(
            commands::ClickedMissionProducer::AutoCrush,
            IdentityFromId(id, mDispatcher.Epoch()));
    }

}
