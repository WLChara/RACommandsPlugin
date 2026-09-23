#include "ClickedMission/ClickedMissionQueue.h"

#include <algorithm>

namespace ra_commands::commands
{
    namespace
    {
        // 待发期限至少为 30 帧，并向上取整到 FrameSendRate 的整数倍。
        constexpr std::uint32_t MINIMUM_INTENT_TTL_FRAMES = 30;
    }

    ClickedMissionQueue::ClickedMissionQueue(std::size_t maximumPending, std::uint32_t epoch)
        : mMaximumPending(maximumPending), mEpoch(epoch)
    {
    }

    ClickedMissionEnqueueResult ClickedMissionQueue::Enqueue(const ClickedMissionIntent& intent)
    {
        const auto hasMatchingEpoch = [&intent](const ClickedMissionIdentity& identity)
        {
            return identity.Epoch == intent.Epoch;
        };

        if (intent.Epoch != mEpoch || !hasMatchingEpoch(intent.Actor) ||
            (intent.Target && !hasMatchingEpoch(*intent.Target)) ||
            (intent.TargetCell && !hasMatchingEpoch(*intent.TargetCell)) ||
            (intent.Nearest && !hasMatchingEpoch(*intent.Nearest)))
        {
            ++mCounters.RejectedWrongEpoch;
            return ClickedMissionEnqueueResult::WrongEpoch;
        }

        for (const auto& pending : mPending)
        {
            if (HasSameSemanticKey(pending, intent))
            {
                ++mCounters.Duplicates;
                return ClickedMissionEnqueueResult::Duplicate;
            }
        }

        if (mPending.size() >= mMaximumPending)
        {
            ++mCounters.RejectedFull;
            return ClickedMissionEnqueueResult::Full;
        }

        mPending.push_back(intent);
        ++mCounters.Enqueued;
        return ClickedMissionEnqueueResult::Enqueued;
    }

    ClickedMissionDrainResult ClickedMissionQueue::Drain(
        std::uint32_t currentFrame,
        const NativeFreeCount& getNativeFreeCount,
        const ValidateIntent& validate,
        const IssueIntent& issue)
    {
        ClickedMissionDrainResult result;

        while (!mPending.empty())
        {
            const auto& head = mPending.front();
            const auto age = static_cast<std::uint32_t>(currentFrame - head.CreatedFrame);
            if (age >= ExpiryTtl(head.FrameSendRate))
            {
                mPending.pop_front();
                ++result.Expired;
                ++mCounters.Expired;
                continue;
            }

            if (!validate(head))
            {
                mPending.pop_front();
                ++result.RejectedByValidation;
                ++mCounters.RejectedByValidation;
                continue;
            }

            if (getNativeFreeCount() < MINIMUM_NATIVE_FREE)
            {
                result.WasStoppedForCapacity = true;
                break;
            }

            const auto intent = head;
            mPending.pop_front();
            ++result.Attempted;
            ++mCounters.Attempted;
            issue(intent);
        }

        return result;
    }

    std::size_t ClickedMissionQueue::CancelByProducer(ClickedMissionProducer producer)
    {
        const auto before = mPending.size();
        mPending.erase(std::remove_if(mPending.begin(), mPending.end(),
            [producer](const ClickedMissionIntent& intent)
            {
                return intent.Producer == producer;
            }), mPending.end());
        const auto cancelled = before - mPending.size();
        mCounters.Cancelled += cancelled;
        return cancelled;
    }

    void ClickedMissionQueue::Reset(std::uint32_t epoch)
    {
        mCounters.ClearedByReset += mPending.size();
        mPending.clear();
        mEpoch = epoch;
    }

    std::size_t ClickedMissionQueue::Size() const noexcept
    {
        return mPending.size();
    }

    std::size_t ClickedMissionQueue::MaximumPending() const noexcept
    {
        return mMaximumPending;
    }

    std::uint32_t ClickedMissionQueue::Epoch() const noexcept
    {
        return mEpoch;
    }

    const ClickedMissionQueueCounters& ClickedMissionQueue::Counters() const noexcept
    {
        return mCounters;
    }

    std::uint32_t ClickedMissionQueue::ExpiryTtl(std::int32_t frameSendRate) noexcept
    {
        const auto rate = frameSendRate <= 0
            ? MINIMUM_INTENT_TTL_FRAMES
            : static_cast<std::uint32_t>(frameSendRate);
        const auto intervals = MINIMUM_INTENT_TTL_FRAMES / rate +
            (MINIMUM_INTENT_TTL_FRAMES % rate == 0 ? 0u : 1u);
        return intervals * rate;
    }

    bool ClickedMissionQueue::HasSameSemanticKey(
        const ClickedMissionIntent& left,
        const ClickedMissionIntent& right) noexcept
    {
        return left.Epoch == right.Epoch && left.Actor == right.Actor &&
               left.Mission == right.Mission && left.Target == right.Target &&
               left.TargetCell == right.TargetCell && left.Nearest == right.Nearest;
    }
}
