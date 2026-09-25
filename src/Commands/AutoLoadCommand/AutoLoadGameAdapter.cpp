#include "Commands/AutoLoadCommand/AutoLoadGameAdapter.h"
#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <HouseClass.h>
#include <WeaponTypeClass.h>
#include <Windows.h>

#include <cstdint>
#include <string>
#include <utility>

namespace ra_commands::game
{
    namespace
    {
        // FVModule 的“三格射程”条件以 leptons 存储，游戏每格为 256 leptons。
        constexpr int MIN_PRIMARY_WEAPON_RANGE_LEPTONS = 3 * 256;

        autoload::UnitKind GetUnitKind(const TechnoClass* techno)
        {
            switch (techno->WhatAmI())
            {
            case AbstractType::Infantry: return autoload::UnitKind::Infantry;
            case AbstractType::Unit: return autoload::UnitKind::Vehicle;
            case AbstractType::Aircraft: return autoload::UnitKind::Aircraft;
            default: return autoload::UnitKind::Other;
            }
        }

        autoload::Unit MakeUnitSnapshot(TechnoClass* techno, HouseClass* local)
        {
            autoload::Unit unit;
            unit.Id = techno->UniqueID;
            unit.Address = reinterpret_cast<std::uintptr_t>(techno);
            unit.Kind = GetUnitKind(techno);
            unit.HasOwner = techno->Owner != nullptr;
            unit.IsLocalOrAllied = techno->Owner && local &&
                (techno->Owner == local || techno->Owner->IsAlliedWith(local));
            unit.IsInPlayfield = techno->IsInPlayfield;
            unit.IsInTransport = techno->Transporter != nullptr;
            unit.IsEnteringTransport = techno->CurrentMission == Mission::Enter ||
                techno->QueuedMission == Mission::Enter;
            unit.PassengerCount = techno->Passengers.NumPassengers;

            if (auto* const type = techno->GetTechnoType())
            {
                unit.HasType = true;
                unit.TypeName = type->ID;
                unit.Size = type->Size;
                unit.SizeLimit = type->SizeLimit;
                unit.PassengerCapacity = type->Passengers;
                unit.UsesFlyingMovement = type->MovementZone == MovementZone::Fly;
                unit.HasIfvMode = type->IFVMode != 0;
                unit.IsOpenTopped = type->OpenTopped;
                if (unit.Kind == autoload::UnitKind::Infantry)
                {
                    unit.IsDog = static_cast<InfantryTypeClass*>(type)->Doggie;
                }
            }

            if (unit.Kind == autoload::UnitKind::Infantry ||
                unit.Kind == autoload::UnitKind::Vehicle)
            {
                unit.IsArmed = techno->IsArmed();
                const auto* const weapon = techno->GetWeapon(0);
                unit.HasPrimaryWeaponRangeAtLeast3 = weapon && weapon->WeaponType &&
                    weapon->WeaponType->Range >= MIN_PRIMARY_WEAPON_RANGE_LEPTONS;
            }

            CoordStruct coords{};
            techno->GetTargetCoords(&coords);
            unit.X = coords.X;
            unit.Y = coords.Y;
            unit.Z = coords.Z;
            return unit;
        }
    }

    bool AutoLoadGameAdapter::CaptureSnapshot(autoload::Snapshot& outSnapshot) const
    {
        if (!IsGameSessionReady())
        {
            return false;
        }

        autoload::Snapshot snapshot;
        auto* const local = HouseClass::Player.get();
        auto* const technos = TechnoClass::Array.get();
        snapshot.Units.reserve(technos->Count);
        for (int index = 0; index < technos->Count; ++index)
        {
            auto* const techno = technos->Items[index];
            if (!techno || techno->IsDead())
            {
                continue;
            }
            const auto kind = GetUnitKind(techno);
            if (kind != autoload::UnitKind::Infantry &&
                kind != autoload::UnitKind::Vehicle)
            {
                continue;
            }
            snapshot.Units.push_back(MakeUnitSnapshot(techno, local));
            const auto& unit = snapshot.Units.back();
            if (!unit.IsLocalOrAllied)
            {
                continue;
            }
            if (unit.Kind == autoload::UnitKind::Infantry)
            {
                snapshot.FriendlyPassengers.push_back(unit.Id);
            }
            else if (unit.Kind == autoload::UnitKind::Vehicle)
            {
                snapshot.FriendlyTransports.push_back(unit.Id);
            }
        }

        auto* const infantry = InfantryClass::Array.get();
        for (int index = 0; index < infantry->Count; ++index)
        {
            auto* const unit = infantry->Items[index];
            if (unit && unit->IsSelected)
            {
                snapshot.SelectedInfantries.push_back(unit->UniqueID);
            }
        }

        auto* const vehicles = UnitClass::Array.get();
        for (int index = 0; index < vehicles->Count; ++index)
        {
            auto* const unit = vehicles->Items[index];
            if (unit && unit->IsSelected)
            {
                snapshot.SelectedVehicles.push_back(unit->UniqueID);
            }
        }

        outSnapshot = std::move(snapshot);
        return true;
    }

    std::uint64_t AutoLoadGameAdapter::GetCurrentTimeMs() const
    {
        return GetTickCount64();
    }

    bool AutoLoadGameAdapter::MakeEnterIntent(
        autoload::UnitId passengerId,
        autoload::UnitId transportId,
        std::uint32_t epoch,
        commands::ClickedMissionIntent& outIntent) const
    {
        auto* const passenger = FindLiveTechno(passengerId);
        auto* const transport = FindLiveTechno(transportId);
        if (!CanEnterTransport(passenger, transport))
        {
            return false;
        }

        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(passenger, epoch);
        intent.Mission = static_cast<std::int32_t>(Mission::Enter);
        intent.TargetCell = CaptureIdentity(transport, epoch);
        intent.Producer = commands::ClickedMissionProducer::AutoLoad;
        intent.Epoch = epoch;
        intent.CreatedFrame = GetCurrentGameFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        outIntent = intent;
        return true;
    }

    void AutoLoadGameAdapter::Deselect(autoload::UnitId id) const
    {
        if (auto* const techno = FindLiveTechno(id))
        {
            techno->Deselect();
        }
    }

}
