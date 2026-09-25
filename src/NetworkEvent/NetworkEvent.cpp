#include "NetworkEvent/NetworkEvent.h"

#include <cstring>

namespace ra_commands::network_event
{
    std::optional<NetworkEvent> BuildProduceEvent(
        std::uint8_t houseIndex, std::int32_t typeIndex, bool isNaval)
    {
        if (typeIndex < 0)
        {
            return std::nullopt;
        }

        NetworkEvent event{};
        event.Kind = static_cast<std::uint8_t>(NetworkEventKind::Produce);
        event.HouseIndex = houseIndex;
        const ProductionPayload payload{
            BUILDING_TYPE_ABSTRACT_ID, typeIndex, isNaval ? 1 : 0
        };
        std::memcpy(event.Data.Raw, &payload, sizeof(payload));
        return event;
    }

    std::optional<NetworkEvent> BuildPlaceEvent(
        std::uint8_t houseIndex, std::int32_t typeIndex, bool isNaval,
        std::int16_t cellX, std::int16_t cellY)
    {
        if (typeIndex < 0)
        {
            return std::nullopt;
        }

        NetworkEvent event{};
        event.Kind = static_cast<std::uint8_t>(NetworkEventKind::Place);
        event.HouseIndex = houseIndex;
        // PLACE 的 Type 是已完成对象 Building；PRODUCE 使用 BuildingType。
        const PlacePayload payload{
            {BUILDING_ABSTRACT_ID, typeIndex, isNaval ? 1 : 0},
            {cellX, cellY}
        };
        std::memcpy(event.Data.Raw, &payload, sizeof(payload));
        return event;
    }

    bool TryAppendNativeEvent(NativeOutListView outList,
        const NetworkEvent& event, std::uint32_t frame,
        std::uint32_t nowMs) noexcept
    {
        if (!outList.Count || !outList.NextIndex || !outList.Events ||
            !outList.EventTimestampsMs)
        {
            return false;
        }

        const auto count = *outList.Count;
        const auto nextIndex = *outList.NextIndex;
        if (count < 0 || count >= NATIVE_OUTLIST_CAPACITY ||
            nextIndex < 0 || nextIndex >= NATIVE_OUTLIST_CAPACITY)
        {
            return false;
        }

        NetworkEvent queued = event;
        queued.Timestamp = frame;
        std::memcpy(&outList.Events[nextIndex], &queued, sizeof(queued));
        outList.EventTimestampsMs[nextIndex] = nowMs;
        *outList.Count = count + 1;
        *outList.NextIndex = (nextIndex + 1) & (NATIVE_OUTLIST_CAPACITY - 1);
        return true;
    }
}
