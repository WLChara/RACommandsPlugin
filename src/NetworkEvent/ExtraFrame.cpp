#include "NetworkEvent/ExtraFrame.h"
#include "NetworkEvent/EventBacklog.h"

#include <algorithm>
#include <cstring>

namespace ra_commands::network_event
{
    namespace
    {
        void WriteTarget(std::uint8_t* destination, TargetPayload target) noexcept
        {
            std::memcpy(destination, &target.RuntimeId, sizeof(target.RuntimeId));
            destination[4] = target.AbstractKind;
        }
    }

    NetworkEvent BuildCapacityMegaMission(std::uint8_t house,
        std::uint32_t frame, int mission, TargetPayload actor,
        TargetPayload target, TargetPayload destination) noexcept
    {
        NetworkEvent event{};
        event.Kind = 4;
        event.HouseIndex = house;
        event.Timestamp = frame;
        WriteTarget(event.Data.Raw, actor);
        event.Data.Raw[5] = static_cast<std::uint8_t>(mission);
        WriteTarget(event.Data.Raw + 7, target);
        WriteTarget(event.Data.Raw + 12, destination);
        // 当前646E90原生构造器将selfId再写入followId，followTag及planning flag为0。
        std::memcpy(event.Data.Raw + 17, &actor.RuntimeId, sizeof(actor.RuntimeId));
        return event;
    }

    NetworkEvent BuildCapacityMegaMissionF(std::uint8_t house,
        std::uint32_t frame, int mission, TargetPayload actor,
        TargetPayload target, TargetPayload destination, int speed, int maximumSpeed) noexcept
    {
        NetworkEvent event{};
        event.Kind = 5;
        event.HouseIndex = house;
        event.Timestamp = frame;
        WriteTarget(event.Data.Raw, actor);
        event.Data.Raw[5] = static_cast<std::uint8_t>(mission);
        WriteTarget(event.Data.Raw + 6, target);
        WriteTarget(event.Data.Raw + 11, destination);
        std::memcpy(event.Data.Raw + 16, &speed, sizeof(speed));
        std::memcpy(event.Data.Raw + 20, &maximumSpeed, sizeof(maximumSpeed));
        return event;
    }

    std::optional<NetworkEvent> BuildCapacitySimpleEvent(std::uint8_t house,
        std::uint32_t frame, std::uint8_t kind, TargetPayload actor) noexcept
    {
        if (!IsCapacitySimpleEvent(kind) || actor.RuntimeId == 0 ||
            actor.AbstractKind != NATIVE_TECHNO_TARGET_KIND)
        {
            return std::nullopt;
        }
        NetworkEvent event{};
        event.Kind = kind;
        event.HouseIndex = house;
        event.Timestamp = frame;
        WriteTarget(event.Data.Raw, actor);
        return event;
    }

    std::size_t GetCapacityCompressedEventSize(const NetworkEvent& event) noexcept
    {
        // MegaMission有一字节聚合数量；MegaMissionF仅Kind+24字节载荷。
        if (event.Kind == 4 || event.Kind == 5)
        {
            return 25;
        }
        return IsCapacitySimpleEvent(event.Kind) ? 6 : 0;
    }

    bool TryGetAlignedFrameTimestamp(std::uint32_t frame,
        std::uint32_t delay, std::uint32_t rate, std::uint32_t& outTimestamp) noexcept
    {
        if (rate == 0 || rate >= 0x80000000u || delay >= 0x80000000u)
        {
            return false;
        }
        // 原生以32位无符号运算向上对齐；溢出后不擅自换用64位公式。
        const auto timestamp = ((frame + delay + rate - 1u) / rate) * rate;
        const auto distance = timestamp - frame;
        if (distance >= 0x80000000u || timestamp % rate != 0)
        {
            return false;
        }
        outTimestamp = timestamp;
        return true;
    }

    bool IsFrameDue(std::uint32_t timestamp, std::uint32_t frame) noexcept
    {
        return frame - timestamp < 0x80000000u;
    }

    std::uint32_t GetExtraMegaBudget(std::uint32_t existingMega) noexcept
    {
        return existingMega >= NATIVE_MEGA_EVENTS_PER_HOUSE_FRAME ? 0u :
            (std::min)(static_cast<std::uint32_t>(EXTRA_EVENTS_PER_TIMESTAMP),
                NATIVE_MEGA_EVENTS_PER_HOUSE_FRAME - existingMega);
    }

    bool BuildExtraFramePacket(const ExtraFrameHeader& header,
        const std::vector<NetworkEvent>& events, std::size_t capacityBytes,
        std::vector<std::uint8_t>& outPacket)
    {
        if (events.empty() || events.size() > EXTRA_EVENTS_PER_TIMESTAMP ||
            capacityBytes < SHORT_FRAME_INFO_SIZE)
        {
            return false;
        }
        auto packetSize = SHORT_FRAME_INFO_SIZE;
        for (const auto& event : events)
        {
            const auto size = GetCapacityCompressedEventSize(event);
            if (size == 0 || event.HouseIndex != header.mHouseIndex ||
                size > capacityBytes - packetSize)
            {
                return false;
            }
            packetSize += size;
        }
        std::vector<std::uint8_t> packet(packetSize, 0);
        NetworkEvent frameInfo{};
        frameInfo.Kind = 0x1C;
        frameInfo.HouseIndex = header.mHouseIndex;
        frameInfo.Timestamp = header.mTimestamp;
        frameInfo.Data.FrameInfo = {header.mChecksum, header.mCommandCount, header.mDelay};
        std::memcpy(packet.data(), &frameInfo, SHORT_FRAME_INFO_SIZE);
        auto offset = SHORT_FRAME_INFO_SIZE;
        for (const auto& event : events)
        {
            packet[offset++] = event.Kind;
            if (event.Kind == 4)
            {
                packet[offset++] = 1;
            }
            const auto payloadSize = event.Kind == 4 ? 23u : event.Kind == 5 ? 24u : 5u;
            std::memcpy(packet.data() + offset, event.Data.Raw, payloadSize);
            offset += payloadSize;
        }
        outPacket.swap(packet);
        return true;
    }

    bool ExtraFrameWindow::TryBegin(std::uint32_t currentFrame,
        std::uint32_t targetFrame, std::uint32_t rate) noexcept
    {
        if (mRequiresFallback || rate == 0 || targetFrame % rate != 0 ||
            targetFrame - currentFrame >= 0x80000000u)
        {
            return false;
        }
        if (mHasTimestamp)
        {
            const auto distance = targetFrame - mLastTimestamp;
            if (distance == 0 || distance >= 0x80000000u)
            {
                return false;
            }
        }
        mLastTimestamp = targetFrame;
        mHasTimestamp = true;
        return true;
    }

    void ExtraFrameWindow::ObserveTiming(std::uint32_t currentFrame,
        std::uint32_t rate, std::uint32_t delay, int packetMode) noexcept
    {
        const bool changed = mHasTiming &&
            (rate != mRate || delay != mDelay || packetMode != mPacketMode);
        mHasTiming = true;
        mRate = rate;
        mDelay = delay;
        mPacketMode = packetMode;
        const auto elapsed = currentFrame - mLastSentTimestamp;
        const bool oldEventsPassed = !mHasSentTimestamp ||
            (elapsed != 0 && elapsed < 0x80000000u);
        if (changed)
        {
            mRequiresFallback = !oldEventsPassed;
            if (oldEventsPassed) mHasTimestamp = false;
        }
        if (mRequiresFallback && oldEventsPassed)
        {
            mRequiresFallback = false;
            mHasTimestamp = false;
        }
    }

    void ExtraFrameWindow::MarkSent(std::uint32_t targetFrame) noexcept
    {
        mLastSentTimestamp = targetFrame;
        mHasSentTimestamp = true;
    }

    bool ExtraFrameWindow::RequiresNativeFallback() const noexcept
    {
        return mRequiresFallback;
    }

    void ExtraFrameWindow::Reset() noexcept
    {
        mLastTimestamp = 0;
        mHasTimestamp = false;
        mHasTiming = false;
        mHasSentTimestamp = false;
        mRequiresFallback = false;
    }
}
