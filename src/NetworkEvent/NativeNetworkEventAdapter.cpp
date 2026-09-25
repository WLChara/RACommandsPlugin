#include "NetworkEvent/NativeNetworkEventAdapter.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <GeneralDefinitions.h>
#include <BuildingTypeClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <NetworkEvents.h>
#include <Networking.h>
#include <Unsorted.h>

#include <Windows.h>
#include <mmsystem.h>

#include <cstddef>
#include <cstring>
#include <memory>

namespace ra_commands::game
{
    namespace
    {
        using network_event::NetworkEventKind;
        using network_event::ProductionPayload;
        using network_event::PlacePayload;

        // SHA-256 7cd005d2... 样本的 QueueClickedMissionEvent (0x646E90)
        // 使用这些 OutList 地址；仅在目标版本门禁通过后绑定游戏线程。
        constexpr std::uintptr_t NEXT_PACKET_INDEX_ADDRESS = 0xA802D0u;
        constexpr std::uintptr_t QUEUED_EVENTS_ADDRESS = 0xA802D4u;
        constexpr std::uintptr_t QUEUED_TIMESTAMPS_ADDRESS = 0xA83A54u;

        static_assert(sizeof(network_event::NetworkEvent) == sizeof(::NetworkEvent));
        static_assert(offsetof(::NetworkEvent, Checksum) ==
            offsetof(network_event::NetworkEvent, Data));
        static_assert(static_cast<std::uint32_t>(AbstractType::BuildingType) ==
            network_event::BUILDING_TYPE_ABSTRACT_ID);
        static_assert(static_cast<std::uint32_t>(AbstractType::Building) ==
            network_event::BUILDING_ABSTRACT_ID);
        static_assert(static_cast<std::uint8_t>(NetworkEvents::Produce) ==
            static_cast<std::uint8_t>(NetworkEventKind::Produce));
        static_assert(static_cast<std::uint8_t>(NetworkEvents::Place) ==
            static_cast<std::uint8_t>(NetworkEventKind::Place));

        bool IsSupportedLocalEvent(const network_event::NetworkEvent& event)
        {
            const auto kind = static_cast<NetworkEventKind>(event.Kind);
            if (kind != NetworkEventKind::Produce && kind != NetworkEventKind::Place)
            {
                return false;
            }

            ProductionPayload payload{};
            std::memcpy(&payload, event.Data.Raw, sizeof(payload));
            auto* const types = BuildingTypeClass::Array.get();
            const bool typeMatches = kind == NetworkEventKind::Place
                ? payload.Type == network_event::BUILDING_ABSTRACT_ID ||
                    payload.Type == network_event::BUILDING_TYPE_ABSTRACT_ID
                : payload.Type == network_event::BUILDING_TYPE_ABSTRACT_ID;
            if (!typeMatches ||
                payload.TypeIndex < 0 ||
                (payload.IsNaval != 0 && payload.IsNaval != 1) ||
                !types || !types->IsInitialized ||
                payload.TypeIndex >= types->Count ||
                types->Count > types->Capacity || !types->Items ||
                !types->Items[payload.TypeIndex])
            {
                return false;
            }

            if (kind == NetworkEventKind::Place)
            {
                PlacePayload place{};
                std::memcpy(&place, event.Data.Raw, sizeof(place));
                auto* const map = MapClass::Instance.get();
                const CellStruct cell{place.Location.X, place.Location.Y};
                return map && map->CoordinatesLegal(cell) &&
                    map->IsWithinUsableArea(cell, false);
            }
            return true;
        }
    }

    void NativeNetworkEventAdapter::BindGameThread(std::uint32_t threadId) noexcept
    {
        mGameThreadId.store(threadId, std::memory_order_release);
    }

    void NativeNetworkEventAdapter::Reset() noexcept
    {
        mGameThreadId.store(0, std::memory_order_release);
    }

    bool NativeNetworkEventAdapter::TryEnqueueLocal(
        const network_event::NetworkEvent& event) const
    {
        const auto* const localPlayer = HouseClass::Player.get();
        const auto gameThreadId = mGameThreadId.load(std::memory_order_acquire);
        if (gameThreadId == 0 || GetCurrentThreadId() != gameThreadId ||
            !IsGameSessionReady() || !localPlayer ||
            localPlayer->ArrayIndex < 0 || localPlayer->ArrayIndex > 255 ||
            event.HouseIndex != localPlayer->ArrayIndex ||
            !IsSupportedLocalEvent(event))
        {
            return false;
        }

        network_event::NativeOutListView outList{
            std::addressof(Networking::LastEventIndex()),
            reinterpret_cast<std::int32_t*>(NEXT_PACKET_INDEX_ADDRESS),
            reinterpret_cast<network_event::NetworkEvent*>(QUEUED_EVENTS_ADDRESS),
            reinterpret_cast<std::uint32_t*>(QUEUED_TIMESTAMPS_ADDRESS)
        };
        return network_event::TryAppendNativeEvent(outList, event,
            GetCurrentGameFrame(), timeGetTime());
    }
}
