#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace ra_commands::network_event
{
    constexpr std::int32_t NATIVE_OUTLIST_CAPACITY = 128;
    constexpr std::uint32_t BUILDING_ABSTRACT_ID = 6;
    constexpr std::uint32_t BUILDING_TYPE_ABSTRACT_ID = 7;

    enum class NetworkEventKind : std::uint8_t
    {
        Place = 0x0B,
        Produce = 0x0E
    };

#pragma pack(push, 1)
    struct TargetPayload
    {
        std::uint32_t RuntimeId;
        std::uint8_t AbstractKind;
    };

    struct ValuePayload
    {
        std::uint32_t Value;
    };

    struct CellPayload
    {
        std::int16_t X;
        std::int16_t Y;
    };

    struct ProductionPayload
    {
        std::uint32_t Type;
        std::int32_t TypeIndex;
        std::int32_t IsNaval;
    };

    struct PlacePayload
    {
        ProductionPayload Production;
        CellPayload Location;
    };

    struct SpecialPlacePayload
    {
        std::uint32_t SpecialWeaponIndex;
        CellPayload Location;
    };

    struct FrameInfoPayload
    {
        std::uint32_t Checksum;
        std::uint16_t CommandCount;
        std::uint8_t Delay;
    };

    struct TimingPayload
    {
        std::uint16_t TargetFps;
        std::uint16_t FrameDelay;
        std::uint8_t FrameSendRate;
    };

    struct AddressChangePayload
    {
        std::uint8_t PlayerIndex;
        std::uint32_t RawAddress;
    };

    struct ArchivePayload
    {
        TargetPayload First;
        TargetPayload Second;
    };

    // 事件种类决定同一段数据的解释方式；未取证的种类只使用 Raw。
    union NetworkEventData
    {
        std::uint8_t Raw[0x68];
        TargetPayload Target;
        ValuePayload Value;
        CellPayload Cell;
        ProductionPayload Production;
        PlacePayload Place;
        SpecialPlacePayload SpecialPlace;
        FrameInfoPayload FrameInfo;
        TimingPayload Timing;
        AddressChangePayload AddressChange;
        ArchivePayload Archive;
    };

    struct NetworkEvent
    {
        std::uint8_t Kind;
        std::uint8_t Unused;
        std::uint8_t HouseIndex;
        std::uint32_t Timestamp;
        NetworkEventData Data;
    };
#pragma pack(pop)

    static_assert(sizeof(TargetPayload) == 5);
    static_assert(sizeof(CellPayload) == 4);
    static_assert(sizeof(ProductionPayload) == 12);
    static_assert(sizeof(PlacePayload) == 16);
    static_assert(sizeof(SpecialPlacePayload) == 8);
    static_assert(sizeof(FrameInfoPayload) == 7);
    static_assert(sizeof(TimingPayload) == 5);
    static_assert(sizeof(AddressChangePayload) == 5);
    static_assert(sizeof(ArchivePayload) == 10);
    static_assert(sizeof(NetworkEventData) == 0x68);
    static_assert(sizeof(NetworkEvent) == 0x6F);
    static_assert(offsetof(NetworkEvent, Timestamp) == 3);
    static_assert(offsetof(NetworkEvent, Data) == 7);
    static_assert(offsetof(PlacePayload, Location) == 12);
    static_assert(offsetof(FrameInfoPayload, Delay) == 6);
    static_assert(std::is_trivially_copyable_v<NetworkEvent>);
    static_assert(std::is_standard_layout_v<NetworkEvent>);

    [[nodiscard]] std::optional<NetworkEvent> BuildProduceEvent(
        std::uint8_t houseIndex, std::int32_t typeIndex, bool isNaval);
    [[nodiscard]] std::optional<NetworkEvent> BuildPlaceEvent(
        std::uint8_t houseIndex, std::int32_t typeIndex, bool isNaval,
        std::int16_t cellX, std::int16_t cellY);

    struct NativeOutListView
    {
        std::int32_t* Count = nullptr;
        std::int32_t* NextIndex = nullptr;
        NetworkEvent* Events = nullptr;
        std::uint32_t* EventTimestampsMs = nullptr;
    };

    /** 仅在游戏线程调用；true 表示事件已复制到原生 OutList。 */
    [[nodiscard]] bool TryAppendNativeEvent(NativeOutListView outList,
        const NetworkEvent& event, std::uint32_t frame,
        std::uint32_t nowMs) noexcept;
}
