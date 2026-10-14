#include "NetworkEvent/ExtraFrame.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace
{
    void RequireExtra(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

void RunExtraFrameTests()
{
    using namespace ra_commands::network_event;
    const ExtraFrameHeader header{3, 105, 0x10203040, 0x5678, 4};
    const auto base = BuildCapacityMegaMission(3, 99, 2, {123, 52}, {456, 52}, {789, 11});
    const auto fast = BuildCapacityMegaMissionF(3, 99, 1, {124, 52}, {}, {790, 11}, -7, 400);
    const auto idle = *BuildCapacitySimpleEvent(3, 99, 6, {125, 52});
    std::vector<NetworkEvent> events{base, fast, idle};
    std::vector<std::uint8_t> packet{0xAA};
    RequireExtra(!BuildExtraFramePacket(header, events, 69, packet) && packet == std::vector<std::uint8_t>{0xAA},
        "one-byte-short capacity must reject the entire packet without changing output");
    RequireExtra(BuildExtraFramePacket(header, events, 70, packet) && packet.size() == 70,
        "exact packet byte capacity must fit base, fast, and simple events");
    NetworkEvent copied{};
    std::memcpy(&copied, packet.data(), SHORT_FRAME_INFO_SIZE);
    RequireExtra(copied.Kind == 0x1C && copied.HouseIndex == 3 && copied.Timestamp == 105 &&
        copied.Data.FrameInfo.Checksum == header.mChecksum &&
        copied.Data.FrameInfo.CommandCount == header.mCommandCount && copied.Data.FrameInfo.Delay == 4,
        "frame header must preserve native time, house, CRC, command count, and delay");
    RequireExtra(packet[14] == 4 && packet[15] == 1 &&
        std::memcmp(packet.data() + 16, base.Data.Raw, 23) == 0 && packet[39] == 5 &&
        std::memcmp(packet.data() + 40, fast.Data.Raw, 24) == 0 && packet[64] == 6 &&
        std::memcmp(packet.data() + 65, idle.Data.Raw, 5) == 0,
        "wire must use native Mega count=1, raw MegaF, and five-byte simple payloads");
    events.back().HouseIndex = 4;
    RequireExtra(!BuildExtraFramePacket(header, events, 70, packet), "mixed houses must never share a compressed frame");
    events = std::vector<NetworkEvent>(129, idle);
    RequireExtra(!BuildExtraFramePacket(header, events, 4096, packet), "one target frame cannot exceed 128 extra events");
    events = {idle};
    events.front().Kind = 0x60;
    RequireExtra(!BuildExtraFramePacket(header, events, 4096, packet), "custom Ares protocol must remain unsupported");

    std::uint32_t stamp = 777;
    RequireExtra(TryGetAlignedFrameTimestamp(100, 4, 3, stamp) && stamp == 105,
        "timestamp must use the native unsigned ceil alignment");
    RequireExtra(!TryGetAlignedFrameTimestamp(100, 4, 0, stamp) && stamp == 105,
        "zero frame quantum must pause without changing the output timestamp");
    RequireExtra(IsFrameDue(0xFFFFFFFEu, 2) && !IsFrameDue(3, 2), "due checks must preserve unsigned frame wrap");
    RequireExtra(GetExtraMegaBudget(0) == 128 && GetExtraMegaBudget(128) == 128 &&
        GetExtraMegaBudget(200) == 56 && GetExtraMegaBudget(256) == 0,
        "consumer budget must reserve native half and never exceed 256");

    ExtraFrameWindow window;
    window.ObserveTiming(100, 3, 4, 2);
    RequireExtra(window.TryBegin(100, 105, 3) && !window.TryBegin(101, 105, 3) &&
        !window.TryBegin(101, 102, 3) && !window.TryBegin(101, 106, 3),
        "duplicate, backward, and non-aligned target frames must pause");
    window.MarkSent(105);
    window.ObserveTiming(102, 2, 4, 2);
    RequireExtra(window.RequiresNativeFallback() && !window.TryBegin(102, 106, 2),
        "quantum change must hold extras while old events are still in flight");
    window.ObserveTiming(105, 2, 4, 2);
    RequireExtra(window.RequiresNativeFallback(), "same execution frame has not yet passed native consumer");
    window.ObserveTiming(106, 2, 4, 2);
    RequireExtra(!window.RequiresNativeFallback() && window.TryBegin(106, 110, 2),
        "strictly passed old execution frame must allow a fresh timing ledger");
    window.MarkSent(110);
    window.ObserveTiming(107, 2, 3, 1);
    RequireExtra(window.RequiresNativeFallback(), "mode or delay change must retain the same conservative hold");
    window.Reset();
    RequireExtra(window.TryBegin(0xFFFFFFFDu, 0, 1) && window.TryBegin(0, 1, 1),
        "strictly increasing serial timestamps must support wraparound");
}
