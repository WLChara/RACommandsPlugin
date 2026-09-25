#include "NetworkEvent/NetworkEvent.h"

#include <array>
#include <cstring>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
}

void RunNetworkEventTests()
{
    using namespace ra_commands::network_event;

    const auto produce = BuildProduceEvent(3, 42, false);
    const auto place = BuildPlaceEvent(3, 42, true, -12, 77);
    Require(produce.has_value() && place.has_value(),
        "valid construction events must be built");
    Require(!BuildProduceEvent(3, -1, false).has_value() &&
        !BuildPlaceEvent(3, -1, false, 0, 0).has_value(),
        "negative building type indices must be rejected");
    Require(produce->Kind == 0x0E && produce->HouseIndex == 3 &&
        place->Kind == 0x0B && place->HouseIndex == 3,
        "event kind and local house must occupy native header bytes");

    ProductionPayload productionPayload{};
    PlacePayload placePayload{};
    std::memcpy(&productionPayload, produce->Data.Raw, sizeof(productionPayload));
    std::memcpy(&placePayload, place->Data.Raw, sizeof(placePayload));
    Require(productionPayload.Type == BUILDING_TYPE_ABSTRACT_ID &&
        productionPayload.TypeIndex == 42 && productionPayload.IsNaval == 0 &&
        placePayload.Production.Type == BUILDING_ABSTRACT_ID &&
        placePayload.Production.TypeIndex == 42 &&
        placePayload.Production.IsNaval == 1 &&
        placePayload.Location.X == -12 && placePayload.Location.Y == 77,
        "construction payloads must begin at native event offset 7");
    Require(produce->Data.Raw[0] == 7 && produce->Data.Raw[4] == 42 &&
        produce->Data.Raw[8] == 0 &&
        place->Data.Raw[0] == 6 && place->Data.Raw[8] == 1 &&
        place->Data.Raw[12] == 0xF4 && place->Data.Raw[13] == 0xFF &&
        place->Data.Raw[14] == 77 && place->Data.Raw[15] == 0,
        "construction payload byte positions must match the target EXE");
    for (std::size_t index = sizeof(PlacePayload); index < sizeof(place->Data.Raw); ++index)
    {
        Require(place->Data.Raw[index] == 0,
            "unused event payload bytes must be zeroed");
    }

    std::array<NetworkEvent, NATIVE_OUTLIST_CAPACITY> events{};
    std::array<std::uint32_t, NATIVE_OUTLIST_CAPACITY> timestamps{};
    std::int32_t count = 0;
    std::int32_t nextIndex = NATIVE_OUTLIST_CAPACITY - 1;
    NativeOutListView outList{&count, &nextIndex, events.data(), timestamps.data()};
    Require(TryAppendNativeEvent(outList, *produce, 1234, 5678),
        "valid event must enter native ring");
    Require(count == 1 && nextIndex == 0 &&
        events.back().Timestamp == 1234 && timestamps.back() == 5678,
        "ring insertion must stamp the frame and wrap the next index");

    count = NATIVE_OUTLIST_CAPACITY;
    Require(!TryAppendNativeEvent(outList, *place, 1235, 5679) &&
        count == NATIVE_OUTLIST_CAPACITY && nextIndex == 0 &&
        timestamps[0] == 0,
        "full native ring must remain untouched");
    count = -1;
    Require(!TryAppendNativeEvent(outList, *place, 1235, 5679),
        "corrupt negative ring count must be rejected");
    count = 0;
    nextIndex = NATIVE_OUTLIST_CAPACITY;
    Require(!TryAppendNativeEvent(outList, *place, 1235, 5679),
        "out-of-range ring index must be rejected");
    Require(!TryAppendNativeEvent({}, *place, 1235, 5679),
        "missing native ring fields must be rejected");
}
