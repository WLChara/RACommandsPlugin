#include "NetworkEvent/EventBacklog.h"
#include "NetworkEvent/PlanningEvents.h"

#include <algorithm>
#include <cstring>

namespace ra_commands::network_event
{
    bool IsCapacityMegaEvent(const NetworkEvent& event) noexcept
    {
        return event.Kind == 4 || event.Kind == 5;
    }

    bool IsCapacitySimpleEvent(std::uint8_t kind) noexcept
    {
        return kind == 6 || kind == 7 || kind == 9 || kind == 10 || kind == 22;
    }

    bool HasCapacityEventActor(const NetworkEvent& event,
        std::uint32_t actorId, std::uint8_t nativeTargetKind) noexcept
    {
        if (!IsCapacityMegaEvent(event) && !IsCapacitySimpleEvent(event.Kind))
        {
            return false;
        }
        std::uint32_t eventActorId = 0;
        std::memcpy(&eventActorId, event.Data.Raw, sizeof(eventActorId));
        return actorId != 0 && eventActorId == actorId &&
            event.Data.Raw[4] == nativeTargetKind;
    }

    EventBacklog::EventBacklog(std::size_t capacity) : mCapacity(capacity) {}

    bool EventBacklog::Submit(const NetworkEvent& event,
        EventBacklogMetadata metadata) noexcept
    {
        std::lock_guard lock(mMutex);
        if (mEvents.size() >= mCapacity ||
            (!IsCapacityMegaEvent(event) && !IsCapacitySimpleEvent(event.Kind) &&
                !IsPlanningControlEvent(event.Kind)))
        {
            ++mRejected;
            return false;
        }
        try
        {
            mEvents.push_back({event, metadata, mNextSequence});
            ++mNextSequence;
            ++mAccepted;
            return true;
        }
        catch (...)
        {
            ++mRejected;
            return false;
        }
    }

    EventBacklogSnapshot EventBacklog::BeginSnapshot(std::uint32_t frame) noexcept
    {
        std::lock_guard lock(mMutex);
        if (mConsuming || (mHasSnapshotFrame && mSnapshotFrame == frame))
        {
            return {};
        }
        mSnapshotFrame = frame;
        mHasSnapshotFrame = true;
        return {mGeneration, mNextSequence - 1, true};
    }

    std::vector<NetworkEvent> EventBacklog::CopyPrefix(
        EventBacklogSnapshot snapshot, std::size_t maximumCount) const
    {
        std::lock_guard lock(mMutex);
        std::vector<NetworkEvent> events;
        if (!snapshot.mValid || snapshot.mGeneration != mGeneration)
        {
            return events;
        }
        events.reserve((std::min)(maximumCount, mEvents.size()));
        for (const auto& pending : mEvents)
        {
            if (events.size() == maximumCount || pending.mSequence > snapshot.mLastSequence)
            {
                break;
            }
            events.push_back(pending.mEvent);
        }
        return events;
    }

    bool EventBacklog::ConsumePrefix(EventBacklogSnapshot snapshot,
        std::size_t count, const SubmitBatch& submit)
    {
        std::lock_guard lock(mMutex);
        if (!submit || count == 0 || mConsuming || !snapshot.mValid ||
            snapshot.mGeneration != mGeneration || count > mEvents.size() ||
            mEvents[count - 1].mSequence > snapshot.mLastSequence)
        {
            return false;
        }
        std::vector<NetworkEvent> events;
        events.reserve(count);
        const auto lastSequence = mEvents[count - 1].mSequence;
        for (std::size_t index = 0; index < count; ++index)
        {
            events.push_back(mEvents[index].mEvent);
        }
        mConsuming = true;
        bool accepted = false;
        try
        {
            accepted = submit(events);
        }
        catch (...)
        {
            mConsuming = false;
            throw;
        }
        mConsuming = false;
        if (!accepted)
        {
            return false;
        }
        // 提交回调中 Clear 或单体取消可能改变前缀；绝不弹出新代次/再入记录。
        if (snapshot.mGeneration == mGeneration)
        {
            while (!mEvents.empty() && mEvents.front().mSequence <= lastSequence)
            {
                mEvents.pop_front();
            }
        }
        mConsumed += count;
        return true;
    }

    void EventBacklog::Clear() noexcept
    {
        std::lock_guard lock(mMutex);
        ++mGeneration;
        mCancelled += mEvents.size();
        mEvents.clear();
        // 保留当前逻辑帧标记，防止回调清理后的新事件同帧提前消费。
    }

    template<typename TPredicate>
    std::uint32_t EventBacklog::CancelMatching(TPredicate predicate) noexcept
    {
        std::lock_guard lock(mMutex);
        const auto before = mEvents.size();
        mEvents.erase(std::remove_if(mEvents.begin(), mEvents.end(), predicate), mEvents.end());
        const auto cancelled = before - mEvents.size();
        mCancelled += cancelled;
        return static_cast<std::uint32_t>(cancelled);
    }

    std::uint32_t EventBacklog::CancelActor(std::uint32_t actorId,
        std::uint8_t nativeTargetKind) noexcept
    {
        return CancelMatching([=](const PendingEvent& event) {
            // 规划追加不是新的普通Move替换：后续节点或原生NoStop不能删除尚未发送的旧节点。
            return !IsPlanningAppendEvent(event.mEvent) &&
                HasCapacityEventActor(event.mEvent, actorId, nativeTargetKind);
        });
    }

    std::uint32_t EventBacklog::CancelProducer(std::uint8_t producerTag) noexcept
    {
        return CancelMatching([=](const PendingEvent& event) {
            return event.mMetadata.mProducerTag == producerTag;
        });
    }

    std::uint32_t EventBacklog::CancelProducerActor(std::uint8_t producerTag,
        std::uint32_t actorId, std::uint8_t nativeTargetKind) noexcept
    {
        return CancelMatching([=](const PendingEvent& event) {
            return event.mMetadata.mProducerTag == producerTag &&
                HasCapacityEventActor(event.mEvent, actorId, nativeTargetKind);
        });
    }

    bool EventBacklog::HasActor(std::uint32_t actorId,
        std::uint8_t nativeTargetKind) const noexcept
    {
        std::lock_guard lock(mMutex);
        return std::any_of(mEvents.begin(), mEvents.end(), [=](const PendingEvent& event) {
            return HasCapacityEventActor(event.mEvent, actorId, nativeTargetKind);
        });
    }

    bool EventBacklog::HasProducerActor(std::uint8_t producerTag,
        std::uint32_t actorId, std::uint8_t nativeTargetKind,
        std::uint32_t intentEpoch) const noexcept
    {
        std::lock_guard lock(mMutex);
        return std::any_of(mEvents.begin(), mEvents.end(), [=](const PendingEvent& event) {
            return event.mMetadata.mProducerTag == producerTag &&
                event.mMetadata.mIntentEpoch == intentEpoch &&
                HasCapacityEventActor(event.mEvent, actorId, nativeTargetKind);
        });
    }

    EventBacklogStats EventBacklog::GetStats() const noexcept
    {
        std::lock_guard lock(mMutex);
        return {mGeneration, mAccepted, mConsumed, mCancelled, mRejected,
            mEvents.size(), mCapacity};
    }
}
