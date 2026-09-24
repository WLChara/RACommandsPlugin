#include "Commands/AutoRepairCommand/AutoRepairGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"

#include <YRPPCore.h>
#include <BuildingClass.h>
#include <HouseClass.h>

#include <utility>

namespace ra_commands::game
{
    namespace
    {
        bool IsLiveBuilding(const BuildingClass* building)
        {
            return building && building->Type && building->IsAlive &&
                building->IsOnMap && !building->InLimbo &&
                building->IsInPlayfield && !building->IsDead() &&
                building->ActuallyPlacedOnMap && !building->BeingProduced;
        }

        bool IsDamaged(const BuildingClass* building)
        {
            const auto* const type = building->GetType();
            return type && building->Health > 0 && type->Strength > 0 &&
                building->Health < type->Strength;
        }
    }

    bool AutoRepairGameAdapter::IsMatchReady() const
    {
        return IsTeslaChargeSessionReady();
    }

    std::uintptr_t AutoRepairGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t AutoRepairGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    bool AutoRepairGameAdapter::CaptureSnapshot(auto_repair::Snapshot& outSnapshot) const
    {
        if (!IsMatchReady())
        {
            return false;
        }

        auto_repair::Snapshot snapshot;
        snapshot.LocalOwner = reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
        auto* const buildings = BuildingClass::Array.get();
        snapshot.Buildings.reserve(buildings->Count);
        for (int index = 0; index < buildings->Count; ++index)
        {
            auto* const building = buildings->Items[index];
            if (!IsLiveBuilding(building))
            {
                continue;
            }

            const bool isDamaged = IsDamaged(building);
            const auto owner = reinterpret_cast<std::uintptr_t>(building->Owner);
            snapshot.Buildings.push_back({
                {reinterpret_cast<std::uintptr_t>(building), building->UniqueID},
                owner,
                isDamaged,
                building->IsBeingRepaired,
                owner == snapshot.LocalOwner && isDamaged &&
                    !building->IsBeingRepaired && building->CanBeRepaired()
            });
        }

        outSnapshot = std::move(snapshot);
        return true;
    }

    std::uint32_t AutoRepairGameAdapter::GetNativeFreeSlots() const
    {
        return GetNativeEventFreeSlots();
    }

    bool AutoRepairGameAdapter::TryRepair(auto_repair::BuildingId id) const
    {
        if (!IsMatchReady() || id.Address == 0 || id.UniqueId == 0)
        {
            return false;
        }

        auto* const techno = FindLiveTechno(id.UniqueId);
        if (!techno || reinterpret_cast<std::uintptr_t>(techno) != id.Address ||
            techno->WhatAmI() != AbstractType::Building)
        {
            return false;
        }
        auto* const building = static_cast<BuildingClass*>(techno);
        if (!IsLiveBuilding(building) || building->Owner != HouseClass::Player.get() ||
            !IsDamaged(building) || building->IsBeingRepaired ||
            !building->CanBeRepaired() ||
            GetNativeEventFreeSlots() < auto_repair::MIN_NATIVE_FREE_SLOTS)
        {
            return false;
        }

        // Repair() 的布尔返回值不证明事件已进入原生 OutList；调用即开始节流。
        (void)building->Repair();
        return true;
    }
}
