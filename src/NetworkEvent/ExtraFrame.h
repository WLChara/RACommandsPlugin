#pragma once

#include "NetworkEvent/NetworkEvent.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::network_event
{
    constexpr std::size_t SHORT_FRAME_INFO_SIZE = 14;
    // 原版 DoList 的单次 House 扫描使用256槽 MegaMission缓冲，不代表整次操作只能控制256个单位。
    // 真实对局是否会超出该批容量尚待具体复现；取得事件种类、House、执行帧和消费数量后再处理。
    constexpr std::uint32_t NATIVE_MEGA_EVENTS_PER_HOUSE_FRAME = 256;
    // 原生/其他生产者保留128个同目标帧Mega槽；插件自己的普通producer已全部接管。
    constexpr std::size_t EXTRA_EVENTS_PER_TIMESTAMP = 128;
    constexpr std::uint8_t NATIVE_TECHNO_TARGET_KIND = 52;

    struct ExtraFrameHeader
    {
        std::uint8_t mHouseIndex = 0;
        std::uint32_t mTimestamp = 0;
        std::uint32_t mChecksum = 0;
        std::uint16_t mCommandCount = 0;
        std::uint8_t mDelay = 0;
    };

    [[nodiscard]] NetworkEvent BuildCapacityMegaMission(std::uint8_t house,
        std::uint32_t frame, int mission, TargetPayload actor,
        TargetPayload target, TargetPayload destination) noexcept;
    [[nodiscard]] NetworkEvent BuildCapacityMegaMissionF(std::uint8_t house,
        std::uint32_t frame, int mission, TargetPayload actor,
        TargetPayload target, TargetPayload destination, int speed, int maximumSpeed) noexcept;
    [[nodiscard]] std::optional<NetworkEvent> BuildCapacitySimpleEvent(
        std::uint8_t house, std::uint32_t frame, std::uint8_t kind,
        TargetPayload actor) noexcept;
    [[nodiscard]] std::size_t GetCapacityCompressedEventSize(
        const NetworkEvent& event) noexcept;
    /** 与原生压缩接收方相同的事件副本；只保留线上载荷，未传输的尾部字节归零。 */
    [[nodiscard]] std::optional<NetworkEvent> BuildCapacityReceivedEvent(
        const NetworkEvent& event, const ExtraFrameHeader& header) noexcept;
    [[nodiscard]] bool TryGetAlignedFrameTimestamp(std::uint32_t frame,
        std::uint32_t delay, std::uint32_t rate, std::uint32_t& outTimestamp) noexcept;
    [[nodiscard]] bool IsFrameDue(std::uint32_t timestamp,
        std::uint32_t frame) noexcept;
    [[nodiscard]] std::uint32_t GetExtraMegaBudget(std::uint32_t existingMega) noexcept;
    /** 整包预检后一次建构；false时输出不变，不发送部分载荷。 */
    [[nodiscard]] bool BuildExtraFramePacket(const ExtraFrameHeader& header,
        const std::vector<NetworkEvent>& events, std::size_t capacityBytes,
        std::vector<std::uint8_t>& outPacket);

    /** 每目标帧只开放一次额外批次；回退、重复和不合法对齐均暂停，不改变远端规则。 */
    class ExtraFrameWindow final
    {
    public:
        [[nodiscard]] bool TryBegin(std::uint32_t currentFrame,
            std::uint32_t targetFrame, std::uint32_t rate) noexcept;
        /** 参数变化后，在旧extra最远执行帧过去前只走原生分帧，保留未发送FIFO。 */
        void ObserveTiming(std::uint32_t currentFrame, std::uint32_t rate,
            std::uint32_t delay, int packetMode) noexcept;
        void MarkSent(std::uint32_t targetFrame) noexcept;
        [[nodiscard]] bool RequiresNativeFallback() const noexcept;
        void Reset() noexcept;
    private:
        std::uint32_t mLastTimestamp = 0;
        bool mHasTimestamp = false;
        std::uint32_t mRate = 0;
        std::uint32_t mDelay = 0;
        int mPacketMode = -1;
        std::uint32_t mLastSentTimestamp = 0;
        bool mHasTiming = false;
        bool mHasSentTimestamp = false;
        bool mRequiresFallback = false;
    };
}
