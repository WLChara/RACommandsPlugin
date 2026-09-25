#include "Commands/AutoBuild/AutoBuildGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"
#include "NetworkEvent/NativeNetworkEventAdapter.h"
#include "NetworkEvent/NetworkEvent.h"

#include <YRPPCore.h>
#include <BuildingTypeClass.h>
#include <FactoryClass.h>
#include <HouseClass.h>
#include <ObjectClass.h>
#include <TechnoClass.h>

#include <cstring>
#include <utility>

namespace ra_commands::game
{
    namespace
    {
        constexpr std::uint32_t MIN_NATIVE_FREE_SLOTS = 13;
        constexpr int MAX_SANE_FACTORY_COUNT = 100000;

        FactoryClass* GetPrimaryFactory(HouseClass* local,
            auto_build::BuildSlot slot)
        {
            return slot == auto_build::BuildSlot::Defense
                ? local->Primary_ForDefenses : local->Primary_ForBuildings;
        }

        bool IsValidLocalFactory(FactoryClass* factory, HouseClass* local)
        {
            auto* const factories = FactoryClass::Array.get();
            if (!factory || !local || !factories || !factories->IsInitialized ||
                factories->Count < 0 || factories->Count > MAX_SANE_FACTORY_COUNT ||
                factories->Count > factories->Capacity ||
                (factories->Count != 0 && !factories->Items))
            {
                return false;
            }
            for (int index = 0; index < factories->Count; ++index)
            {
                if (factories->Items[index] == factory)
                {
                    return factory->Owner == local;
                }
            }
            return false;
        }

        BuildingTypeClass* GetCurrentBuildingType(FactoryClass* factory)
        {
            TechnoTypeClass* type = factory->Object
                ? factory->Object->GetTechnoType() : nullptr;
            if (!type && factory->QueuedObjects.Count > 0 &&
                factory->QueuedObjects.Items)
            {
                type = factory->QueuedObjects.Items[0];
            }
            return type && type->WhatAmI() == AbstractType::BuildingType
                ? static_cast<BuildingTypeClass*>(type) : nullptr;
        }

        bool IsRegisteredBuildingType(BuildingTypeClass* type)
        {
            auto* const types = BuildingTypeClass::Array.get();
            return type && types && types->IsInitialized &&
                type->ArrayIndex >= 0 && type->ArrayIndex < types->Count &&
                types->Count <= types->Capacity && types->Items &&
                types->Items[type->ArrayIndex] == type;
        }

        bool MatchesSlot(auto_build::BuildSlot slot,
            const BuildingTypeClass* type)
        {
            return (slot == auto_build::BuildSlot::Defense) ==
                (type->BuildCat == BuildCat::Combat);
        }

        bool IsLimited(const BuildingTypeClass* type)
        {
            // 沿用原自动建造规则：BuildLimit 为 1..100 的建筑不自动续建。
            return type->BuildLimit >= 1 && type->BuildLimit <= 100;
        }
    }

    AutoBuildGameAdapter::AutoBuildGameAdapter(NativeNetworkEventAdapter& events)
        : mEvents(events)
    {
    }

    bool AutoBuildGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady() && HouseClass::Player.get() != nullptr;
    }

    std::uintptr_t AutoBuildGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t AutoBuildGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    bool AutoBuildGameAdapter::CaptureSlot(auto_build::BuildSlot slot,
        auto_build::SlotSnapshot& outSnapshot) const
    {
        if (!IsMatchReady())
        {
            return false;
        }
        auto* const local = HouseClass::Player.get();
        auto* const factory = GetPrimaryFactory(local, slot);
        auto_build::SlotSnapshot snapshot;
        if (!factory)
        {
            outSnapshot = std::move(snapshot);
            return true;
        }
        if (!IsValidLocalFactory(factory, local) ||
            factory->QueuedObjects.Count < 0 ||
            factory->QueuedObjects.Count > factory->QueuedObjects.Capacity ||
            (factory->QueuedObjects.Count > 0 && !factory->QueuedObjects.Items))
        {
            return false;
        }

        auto* const type = GetCurrentBuildingType(factory);
        if (type)
        {
            if (!IsRegisteredBuildingType(type))
            {
                return false;
            }
            const char* const name = type->get_ID();
            if (!name)
            {
                return false;
            }
            snapshot.Product = auto_build::ProductId{
                type->ArrayIndex, name, type->Naval
            };
            snapshot.IsCombat = type->BuildCat == BuildCat::Combat;
            snapshot.BuildLimit = type->BuildLimit;
        }

        snapshot.IsReady = factory->Object && factory->IsDone();
        snapshot.IsManuallyStopped = !snapshot.IsReady && factory->IsManual &&
            (factory->IsSuspended || factory->OnHold);
        snapshot.IsInProgress = snapshot.Product.has_value() &&
            !snapshot.IsReady && !snapshot.IsManuallyStopped;
        snapshot.IsEmpty = !snapshot.IsReady && !snapshot.IsInProgress &&
            !factory->Object && factory->QueuedObjects.Count == 0;
        outSnapshot = std::move(snapshot);
        return true;
    }

    bool AutoBuildGameAdapter::TryEnqueueProduce(auto_build::BuildSlot slot,
        const auto_build::ProductId& product) const
    {
        if (!IsMatchReady() || product.TypeIndex < 0 ||
            product.RegisteredName.empty() ||
            GetNativeEventFreeSlots() < MIN_NATIVE_FREE_SLOTS)
        {
            return false;
        }
        auto* const local = HouseClass::Player.get();
        if (local->ArrayIndex < 0 || local->ArrayIndex > 255)
        {
            return false;
        }
        auto* const types = BuildingTypeClass::Array.get();
        if (!types || !types->IsInitialized || types->Count < 0 ||
            types->Count > types->Capacity || !types->Items ||
            product.TypeIndex >= types->Count)
        {
            return false;
        }
        auto* const type = types->Items[product.TypeIndex];
        const char* const name = type ? type->get_ID() : nullptr;
        if (!type || !name || product.RegisteredName != name ||
            type->ArrayIndex != product.TypeIndex ||
            type->Naval != product.IsNaval || !MatchesSlot(slot, type) ||
            IsLimited(type) ||
            local->CanBuild(type, false, false) != CanBuildResult::Buildable)
        {
            return false;
        }

        auto_build::SlotSnapshot current;
        if (!CaptureSlot(slot, current) || !current.IsEmpty ||
            GetNativeEventFreeSlots() < MIN_NATIVE_FREE_SLOTS)
        {
            return false;
        }
        const auto event = network_event::BuildProduceEvent(
            static_cast<std::uint8_t>(local->ArrayIndex),
            product.TypeIndex, product.IsNaval);
        return event && mEvents.TryEnqueueLocal(*event);
    }
}
