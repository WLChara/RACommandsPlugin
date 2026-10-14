#pragma once

#include "NetworkEvent/NetworkEvent.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace ra_commands::network_event
{
    constexpr std::size_t COMMAND_BACKLOG_CAPACITY = 65536;
    constexpr std::uint8_t UNCLASSIFIED_COMMAND_PRODUCER = 0xFF;

    struct EventBacklogMetadata
    {
        std::uint8_t mProducerTag = UNCLASSIFIED_COMMAND_PRODUCER;
        std::uint32_t mIntentEpoch = 0;
    };

    struct EventBacklogSnapshot
    {
        std::uint64_t mGeneration = 0;
        std::uint64_t mLastSequence = 0;
        bool mValid = false;
    };

    struct EventBacklogStats
    {
        std::uint64_t mGeneration = 1;
        std::uint64_t mAccepted = 0;
        std::uint64_t mConsumed = 0;
        std::uint64_t mCancelled = 0;
        std::uint64_t mRejected = 0;
        std::size_t mPending = 0;
        std::size_t mCapacity = 0;
    };

    [[nodiscard]] bool IsCapacityMegaEvent(const NetworkEvent& event) noexcept;
    [[nodiscard]] bool IsCapacitySimpleEvent(std::uint8_t kind) noexcept;
    [[nodiscard]] bool HasCapacityEventActor(const NetworkEvent& event,
        std::uint32_t actorId, std::uint8_t nativeTargetKind) noexcept;

    /**
     * 独占未进入原生队列或发送接口的事件。消费期间持有递归执行门，
     * 跨线程取消等待当前提交完成；同线程回调可取消、清理或追加下一帧事件。
     */
    class EventBacklog final
    {
    public:
        using SubmitBatch = std::function<bool(const std::vector<NetworkEvent>&)>;

        explicit EventBacklog(std::size_t capacity = COMMAND_BACKLOG_CAPACITY);
        bool Submit(const NetworkEvent& event, EventBacklogMetadata metadata) noexcept;
        /** 同一逻辑帧只发出一次快照，再入条目及剩余前缀等待下一帧。 */
        [[nodiscard]] EventBacklogSnapshot BeginSnapshot(std::uint32_t frame) noexcept;
        [[nodiscard]] std::vector<NetworkEvent> CopyPrefix(
            EventBacklogSnapshot snapshot, std::size_t maximumCount) const;
        /** true 表示回调接受的前缀已消费；false 保留当前代次的全部未提交记录。 */
        bool ConsumePrefix(EventBacklogSnapshot snapshot, std::size_t count,
            const SubmitBatch& submit);
        void Clear() noexcept;
        std::uint32_t CancelActor(std::uint32_t actorId, std::uint8_t nativeTargetKind) noexcept;
        std::uint32_t CancelProducer(std::uint8_t producerTag) noexcept;
        std::uint32_t CancelProducerActor(std::uint8_t producerTag,
            std::uint32_t actorId, std::uint8_t nativeTargetKind) noexcept;
        [[nodiscard]] bool HasActor(std::uint32_t actorId,
            std::uint8_t nativeTargetKind) const noexcept;
        [[nodiscard]] bool HasProducerActor(std::uint8_t producerTag,
            std::uint32_t actorId, std::uint8_t nativeTargetKind,
            std::uint32_t intentEpoch) const noexcept;
        [[nodiscard]] EventBacklogStats GetStats() const noexcept;

    private:
        struct PendingEvent
        {
            NetworkEvent mEvent;
            EventBacklogMetadata mMetadata;
            std::uint64_t mSequence;
        };
        template<typename TPredicate>
        std::uint32_t CancelMatching(TPredicate predicate) noexcept;

        mutable std::recursive_mutex mMutex;
        std::deque<PendingEvent> mEvents;
        std::size_t mCapacity;
        std::uint64_t mGeneration = 1;
        std::uint64_t mNextSequence = 1;
        std::uint64_t mAccepted = 0;
        std::uint64_t mConsumed = 0;
        std::uint64_t mCancelled = 0;
        std::uint64_t mRejected = 0;
        std::uint32_t mSnapshotFrame = 0;
        bool mHasSnapshotFrame = false;
        bool mConsuming = false;
    };
}
