#include "ClickedMission/ClickedMissionDispatcher.h"

namespace ra_commands::commands
{
    namespace
    {
        // 只防止原生队列长期拥塞时待发意图无限增长；上限仍需游戏实测。
        constexpr std::size_t MAX_PENDING_CLICKED_MISSIONS = 65536;
    }

    ClickedMissionDispatcher::ClickedMissionDispatcher(IClickedMissionGamePort& game)
        : mGame(game), mQueue(MAX_PENDING_CLICKED_MISSIONS, 1)
    {
    }

    void ClickedMissionDispatcher::OnGameFrame()
    {
        if (!mGame.IsMatchReady())
        {
            if (mIsSessionActive)
            {
                Reset();
            }
            return;
        }

        const auto currentFrame = mGame.GetCurrentFrame();
        const auto sessionIdentity = mGame.GetSessionIdentity();
        // 对局身份变化或帧计数回退后，不允许旧对象身份进入新对局。
        if (!mIsSessionActive || mSessionIdentity != sessionIdentity || currentFrame < mLastFrame)
        {
            for (std::uint8_t tag = 0; tag < static_cast<std::uint8_t>(ClickedMissionProducer::Count); ++tag)
            {
                (void)mGame.CancelSubmittedByProducer(static_cast<ClickedMissionProducer>(tag));
            }
            mQueue.Reset(++mEpoch);
            mSessionIdentity = sessionIdentity;
            mIsSessionActive = true;
        }
        mLastFrame = currentFrame;

        mQueue.Drain(currentFrame,
            [this]() { return mGame.GetNativeFreeSlots(); },
            [this](const ClickedMissionIntent& intent)
            {
                return mGame.ValidateClickedMissionIntent(intent);
            },
            [this](const ClickedMissionIntent& intent)
            {
                mGame.AttemptClickedMission(intent);
            });
    }

    ClickedMissionEnqueueResult ClickedMissionDispatcher::Submit(
        const ClickedMissionIntent& intent)
    {
        if (!mIsSessionActive) { return ClickedMissionEnqueueResult::WrongEpoch; }
        const auto result = mQueue.Enqueue(intent);
        if ((result == ClickedMissionEnqueueResult::Enqueued ||
            result == ClickedMissionEnqueueResult::Replaced) && intent.DestinationCell &&
            intent.Supersession == ClickedMissionSupersession::ReplaceSameProducerActorMission)
        {
            // 新目标替换仍在传输队列里的旧目标，避免旧 Move 在后续帧回活。
            (void)mGame.CancelSubmittedByProducerAndActor(intent.Producer, intent.Actor);
        }
        return result;
    }

    std::size_t ClickedMissionDispatcher::CancelByProducer(ClickedMissionProducer producer)
    {
        return mQueue.CancelByProducer(producer) + mGame.CancelSubmittedByProducer(producer);
    }

    std::size_t ClickedMissionDispatcher::CancelByProducerAndActor(
        ClickedMissionProducer producer, const ClickedMissionIdentity& actor)
    {
        return mQueue.CancelByProducerAndActor(producer, actor) +
            mGame.CancelSubmittedByProducerAndActor(producer, actor);
    }

    bool ClickedMissionDispatcher::HasPendingActor(ClickedMissionProducer producer,
        std::uintptr_t address, std::uint32_t uniqueId) const noexcept
    {
        return mQueue.HasPendingActor(producer, address, uniqueId) ||
            mGame.HasSubmittedActor(producer, address, uniqueId, mEpoch);
    }

    void ClickedMissionDispatcher::Reset()
    {
        for (std::uint8_t tag = 0; tag < static_cast<std::uint8_t>(ClickedMissionProducer::Count); ++tag)
        {
            (void)mGame.CancelSubmittedByProducer(static_cast<ClickedMissionProducer>(tag));
        }
        mQueue.Reset(++mEpoch);
        mSessionIdentity = 0;
        mLastFrame = 0;
        mIsSessionActive = false;
    }

    bool ClickedMissionDispatcher::IsSessionActive() const noexcept
    {
        return mIsSessionActive;
    }

    std::uint32_t ClickedMissionDispatcher::Epoch() const noexcept
    {
        return mEpoch;
    }

    const ClickedMissionQueueCounters& ClickedMissionDispatcher::Counters() const noexcept
    {
        return mQueue.Counters();
    }
}
