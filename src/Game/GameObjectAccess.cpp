#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <BuildingClass.h>
#include <InfantryClass.h>
#include <UnitClass.h>
#include <HouseClass.h>
#include <Networking.h>

#include <limits>
#include <cstring>

namespace ra_commands::game
{
    namespace
    {
        // 仅为防御损坏的游戏数组，非游戏规则上限；发现合法对局超过此值再按证据调整。
        constexpr int MAX_GAME_UNITS = 100000;

        template<typename T>
        bool IsUsableArray(const DynamicVectorClass<T*>* array)
        {
            return array && array->IsInitialized && array->Count >= 0 &&
                array->Count <= MAX_GAME_UNITS && array->Count <= array->Capacity &&
                (array->Count == 0 || array->Items != nullptr);
        }

    }

    bool IsGameSessionReady()
    {
        return HouseClass::Player.get() != nullptr &&
            IsUsableArray(TechnoClass::Array.get()) &&
            IsUsableArray(InfantryClass::Array.get()) &&
            IsUsableArray(UnitClass::Array.get());
    }

    bool IsGameSessionWithBuildingsReady()
    {
        return IsGameSessionReady() && IsUsableArray(BuildingClass::Array.get());
    }

    bool IsLocal(const TechnoClass* techno)
    {
        const auto* const player = HouseClass::Player.get();
        return player && techno && techno->Owner == player;
    }

    bool NameEqual(const TechnoClass* techno, std::string_view registeredName)
    {
        if (!techno || registeredName.empty())
        {
            return false;
        }
        const auto* const type = techno->GetTechnoType();
        const char* const id = type ? type->get_ID() : nullptr;
        return id && std::strlen(id) == registeredName.size() &&
            ::_strnicmp(id, registeredName.data(), registeredName.size()) == 0;
    }

    TechnoClass* FindLiveTechno(std::uint64_t uniqueId)
    {
        if (uniqueId == 0 || uniqueId > (std::numeric_limits<std::uint32_t>::max)())
        {
            return nullptr;
        }

        auto* const technos = TechnoClass::Array.get();
        if (!IsUsableArray(technos))
        {
            return nullptr;
        }
        for (int index = 0; index < technos->Count; ++index)
        {
            auto* const techno = technos->Items[index];
            if (techno && techno->UniqueID == uniqueId)
            {
                return techno;
            }
        }
        return nullptr;
    }

    TechnoClass* ResolveIdentity(const commands::ClickedMissionIdentity& identity)
    {
        auto* const techno = FindLiveTechno(identity.UniqueId);
        return techno && reinterpret_cast<std::uintptr_t>(techno) == identity.Address &&
            static_cast<std::uint32_t>(techno->WhatAmI()) == identity.Kind &&
            !techno->IsDead() ? techno : nullptr;
    }

    commands::ClickedMissionIdentity CaptureIdentity(TechnoClass* techno, std::uint32_t epoch)
    {
        return {
            reinterpret_cast<std::uintptr_t>(techno),
            techno->UniqueID,
            static_cast<std::uint32_t>(techno->WhatAmI()),
            epoch
        };
    }

    bool CanEnterTransport(TechnoClass* passenger, TechnoClass* transport)
    {
        if (!passenger || !transport || passenger == transport ||
            !passenger->Owner || !transport->Owner ||
            !passenger->IsInPlayfield || !transport->IsInPlayfield ||
            passenger->Transporter || transport->WhatAmI() != AbstractType::Unit)
        {
            return false;
        }

        const auto passengerKind = passenger->WhatAmI();
        if (passengerKind != AbstractType::Infantry && passengerKind != AbstractType::Unit)
        {
            return false;
        }

        auto* const passengerType = passenger->GetTechnoType();
        auto* const transportType = transport->GetTechnoType();
        if (!passengerType || !transportType ||
            passengerType->MovementZone == MovementZone::Fly ||
            passengerType->Size > transportType->SizeLimit ||
            transportType->Passengers <= transport->Passengers.NumPassengers)
        {
            return false;
        }

        if (passengerKind == AbstractType::Infantry)
        {
            return !static_cast<InfantryTypeClass*>(passengerType)->Doggie;
        }
        return passenger->Passengers.NumPassengers <= 0 ||
            (passengerType->Passengers > 0 &&
                passenger->Passengers.NumPassengers >= passengerType->Passengers);
    }

    std::uint32_t GetCurrentGameFrame()
    {
        return static_cast<std::uint32_t>(Unsorted::CurrentFrame);
    }

    std::int32_t GetGameFrameSendRate()
    {
        return Networking::FrameSendRate();
    }
}
