#include "ClickedMission/ClickedMissionDispatcher.h"

namespace ra_commands::commands
{
    namespace
    {
        // 只防止原生队列长期拥塞时待发意图无限增长；上限仍需游戏实测。
        constexpr std::size_t MAX_PENDING_CLICKED_MISSIONS = 2048;
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
        return mIsSessionActive
            ? mQueue.Enqueue(intent)
            : ClickedMissionEnqueueResult::WrongEpoch;
    }

    void ClickedMissionDispatcher::Reset()
    {
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
