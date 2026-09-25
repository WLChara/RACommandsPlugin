#include "Commands/AirSpreadCommand/AirSpreadIntentHandler.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <MapClass.h>

namespace ra_commands::game
{
    namespace
    {
        bool IsSupportedAirSpreadMoveIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Producer == commands::ClickedMissionProducer::AirSpread &&
                intent.Mission == static_cast<std::int32_t>(Mission::Move) &&
                !intent.Target && !intent.TargetCell && !intent.Nearest &&
                intent.DestinationCell && intent.Actor.Epoch == intent.Epoch;
        }

        CellClass* ResolveAirSpreadMoveCell(
            const commands::ClickedMissionIntent& intent, TechnoClass*& actor)
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
                static_cast<short>(destination.X), static_cast<short>(destination.Y)};
            if (!map->CoordinatesLegal(coords) ||
                !map->IsWithinUsableArea(coords, false))
            {
                return nullptr;
            }
            auto* const cell = map->TryGetCellAt(coords);
            return cell && cell->MapCoords == coords ? cell : nullptr;
        }
    }

    bool ValidateAirSpreadMoveIntent(const commands::ClickedMissionIntent& intent)
    {
        TechnoClass* actor = nullptr;
        return ResolveAirSpreadMoveCell(intent, actor) != nullptr;
    }

    void AttemptAirSpreadMoveIntent(const commands::ClickedMissionIntent& intent)
    {
        TechnoClass* actor = nullptr;
        auto* const cell = ResolveAirSpreadMoveCell(intent, actor);
        if (cell)
        {
            actor->ClickedMission(Mission::Move, nullptr, cell, nullptr);
        }
    }
}
