#include "Commands/TeslaChargeCommand/TeslaChargeGameAdapter.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <BuildingClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>

#include <utility>

namespace ra_commands::game
{
    namespace
    {
        tesla_charge::ObjectSnapshot CaptureObject(TechnoClass* techno)
        {
            const auto cell = techno->GetMapCoords();
            return {
                techno->UniqueID,
                reinterpret_cast<std::uintptr_t>(techno->Owner),
                cell.X,
                cell.Y
            };
        }
    }

    std::uint32_t TeslaChargeGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    bool TeslaChargeGameAdapter::CaptureSnapshot(tesla_charge::Snapshot& outSnapshot) const
    {
        if (!IsTeslaChargeSessionReady())
        {
            return false;
        }

        tesla_charge::Snapshot snapshot;
        snapshot.LocalOwner = reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());

        auto* const buildings = BuildingClass::Array.get();
        snapshot.Teslas.reserve(buildings->Count);
        for (int index = 0; index < buildings->Count; ++index)
        {
            auto* const building = buildings->Items[index];
            if (IsLocalTesla(building))
            {
                snapshot.Teslas.push_back(CaptureObject(building));
            }
        }

        auto* const infantry = InfantryClass::Array.get();
        snapshot.Chargers.reserve(infantry->Count);
        for (int index = 0; index < infantry->Count; ++index)
        {
            auto* const charger = infantry->Items[index];
            if (IsLocalTeslaCharger(charger))
            {
                snapshot.Chargers.push_back(CaptureObject(charger));
            }
        }

        outSnapshot = std::move(snapshot);
        return true;
    }

    bool TeslaChargeGameAdapter::MakeAttackIntent(
        tesla_charge::UnitId chargerId,
        tesla_charge::UnitId teslaId,
        std::uint32_t epoch,
        commands::ClickedMissionIntent& outIntent) const
    {
        auto* const charger = FindLiveTechno(chargerId);
        auto* const tesla = FindLiveTechno(teslaId);
        if (!CanChargeTesla(charger, tesla))
        {
            return false;
        }

        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(charger, epoch);
        intent.Mission = static_cast<std::int32_t>(Mission::Attack);
        intent.Target = CaptureIdentity(tesla, epoch);
        intent.Producer = commands::ClickedMissionProducer::TeslaCharge;
        intent.Epoch = epoch;
        intent.CreatedFrame = GetCurrentGameFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        outIntent = intent;
        return true;
    }

    bool TeslaChargeGameAdapter::IsTargetingTesla(
        tesla_charge::UnitId chargerId, tesla_charge::UnitId teslaId) const
    {
        auto* const charger = FindLiveTechno(chargerId);
        auto* const tesla = FindLiveTechno(teslaId);
        return CanChargeTesla(charger, tesla) && charger->Target == tesla;
    }

    void TeslaChargeGameAdapter::DeselectIfSelected(tesla_charge::UnitId chargerId) const
    {
        auto* const charger = FindLiveTechno(chargerId);
        if (IsLocalTeslaCharger(charger) && charger->IsSelected)
        {
            charger->Deselect();
        }
    }
}
