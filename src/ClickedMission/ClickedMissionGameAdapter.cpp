#include "ClickedMission/ClickedMissionGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <MapClass.h>

namespace ra_commands::game
{
    namespace
    {
        bool IsSupportedEnterIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Mission == static_cast<std::int32_t>(Mission::Enter) &&
                !intent.Target && intent.TargetCell && !intent.Nearest &&
                !intent.DestinationCell &&
                intent.Actor.Epoch == intent.Epoch &&
                intent.TargetCell->Epoch == intent.Epoch;
        }

        bool IsSupportedTeslaChargeIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Producer == commands::ClickedMissionProducer::TeslaCharge &&
                intent.Mission == static_cast<std::int32_t>(Mission::Attack) &&
                intent.Target && !intent.TargetCell && !intent.Nearest &&
                !intent.DestinationCell &&
                intent.Actor.Epoch == intent.Epoch &&
                intent.Target->Epoch == intent.Epoch;
        }

        bool IsSupportedAirSpreadMoveIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Producer == commands::ClickedMissionProducer::AirSpread &&
                intent.Mission == static_cast<std::int32_t>(Mission::Move) &&
                !intent.Target && !intent.TargetCell && !intent.Nearest &&
                intent.DestinationCell && intent.Actor.Epoch == intent.Epoch;
        }

        CellClass* ResolveAirSpreadMoveCell(
            const commands::ClickedMissionIntent& intent,
            TechnoClass*& actor)
        {
            actor = nullptr;
            if (!IsSupportedAirSpreadMoveIntent(intent))
            {
                return nullptr;
            }

            actor = ResolveIdentity(intent.Actor);
            if (!actor || !actor->Owner || actor->Owner != HouseClass::Player.get() ||
                !actor->IsAlive || !actor->IsOnMap || actor->InLimbo ||
                !actor->IsInPlayfield || actor->IsDead())
            {
                return nullptr;
            }

            const auto kind = actor->WhatAmI();
            if (kind != AbstractType::Aircraft)
            {
                if (kind != AbstractType::Infantry && kind != AbstractType::Unit)
                {
                    return nullptr;
                }
                const auto* const type = actor->GetTechnoType();
                if (!type || (type->MovementZone != MovementZone::Fly &&
                    type->SpeedType != SpeedType::Winged))
                {
                    return nullptr;
                }
            }

            if (actor->Transporter)
            {
                return nullptr;
            }

            // GetCellIndex shifts Y by 9; keep both axes in its 512x512 range.
            const auto destination = *intent.DestinationCell;
            if (destination.X < 0 || destination.X >= 512 ||
                destination.Y < 0 || destination.Y >= 512)
            {
                return nullptr;
            }
            auto* const map = MapClass::Instance.get();
            if (!map)
            {
                return nullptr;
            }
            const CellStruct coords{
                static_cast<short>(destination.X), static_cast<short>(destination.Y) };
            // 排除虽然有 Cell 对象、但不属于可用战场的边缘格。
            if (!map->CoordinatesLegal(coords) ||
                !map->IsWithinUsableArea(coords, false))
            {
                return nullptr;
            }
            auto* const cell = map->TryGetCellAt(coords);
            return cell && cell->MapCoords == coords ? cell : nullptr;
        }
    }

    bool ClickedMissionGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady();
    }

    std::uintptr_t ClickedMissionGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t ClickedMissionGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    std::uint32_t ClickedMissionGameAdapter::GetNativeFreeSlots() const
    {
        return GetNativeEventFreeSlots();
    }

    bool ClickedMissionGameAdapter::ValidateClickedMissionIntent(
        const commands::ClickedMissionIntent& intent) const
    {
        if (IsSupportedEnterIntent(intent))
        {
            return CanEnterTransport(ResolveIdentity(intent.Actor),
                ResolveIdentity(*intent.TargetCell));
        }
        if (IsSupportedTeslaChargeIntent(intent))
        {
            return CanChargeTesla(ResolveIdentity(intent.Actor),
                ResolveIdentity(*intent.Target));
        }
        if (IsSupportedAirSpreadMoveIntent(intent))
        {
            TechnoClass* actor = nullptr;
            return ResolveAirSpreadMoveCell(intent, actor) != nullptr;
        }
        return false;
    }

    void ClickedMissionGameAdapter::AttemptClickedMission(
        const commands::ClickedMissionIntent& intent) const
    {
        if (IsSupportedEnterIntent(intent))
        {
            auto* const passenger = ResolveIdentity(intent.Actor);
            auto* const transport = ResolveIdentity(*intent.TargetCell);
            if (CanEnterTransport(passenger, transport))
            {
                passenger->ClickedMission(Mission::Enter, nullptr, transport, nullptr);
            }
            return;
        }
        if (IsSupportedTeslaChargeIntent(intent))
        {
            auto* const charger = ResolveIdentity(intent.Actor);
            auto* const tesla = ResolveIdentity(*intent.Target);
            if (CanChargeTesla(charger, tesla))
            {
                charger->ClickedMission(Mission::Attack, tesla, nullptr, nullptr);
            }
            return;
        }
        if (IsSupportedAirSpreadMoveIntent(intent))
        {
            TechnoClass* actor = nullptr;
            auto* const cell = ResolveAirSpreadMoveCell(intent, actor);
            if (cell)
            {
                actor->ClickedMission(Mission::Move, nullptr, cell, nullptr);
            }
        }
    }
}
