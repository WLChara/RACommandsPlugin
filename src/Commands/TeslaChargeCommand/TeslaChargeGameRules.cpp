#include "Commands/TeslaChargeCommand/TeslaChargeGameRules.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <BuildingClass.h>
#include <InfantryClass.h>

#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        constexpr std::int64_t MAX_TESLA_CELL_DISTANCE_SQUARED = 32 * 32;

        bool IsLocalLiveObject(TechnoClass* techno)
        {
            return IsLocal(techno) && techno->IsAlive && techno->IsOnMap &&
                !techno->InLimbo && techno->IsInPlayfield && !techno->IsDead();
        }
    }

    bool IsLocalTesla(TechnoClass* techno)
    {
        if (!IsLocalLiveObject(techno) || techno->WhatAmI() != AbstractType::Building)
        {
            return false;
        }

        const auto* const building = static_cast<BuildingClass*>(techno);
        return building->Type && building->ActuallyPlacedOnMap &&
            !building->BeingProduced && NameEqual(building, "TESLA");
    }

    bool IsLocalTeslaCharger(TechnoClass* techno)
    {
        if (!IsLocalLiveObject(techno) || techno->WhatAmI() != AbstractType::Infantry)
        {
            return false;
        }

        const auto* const infantry = static_cast<InfantryClass*>(techno);
        return infantry->Type && !infantry->Transporter &&
            (NameEqual(infantry, "SHK") || NameEqual(infantry, "SHOCK"));
    }

    bool CanChargeTesla(TechnoClass* charger, TechnoClass* tesla)
    {
        if (!IsLocalTeslaCharger(charger) || !IsLocalTesla(tesla) ||
            charger->Owner != tesla->Owner)
        {
            return false;
        }

        const auto chargerCell = charger->GetMapCoords();
        const auto teslaCell = tesla->GetMapCoords();
        const auto dx = static_cast<std::int64_t>(chargerCell.X) - teslaCell.X;
        const auto dy = static_cast<std::int64_t>(chargerCell.Y) - teslaCell.Y;
        return dx * dx + dy * dy <= MAX_TESLA_CELL_DISTANCE_SQUARED;
    }
}
