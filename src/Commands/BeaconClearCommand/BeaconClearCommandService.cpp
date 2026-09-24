#include "Commands/BeaconClearCommand/BeaconClearCommandService.h"

#include <algorithm>
#include <utility>

namespace ra_commands::beacon_clear
{
    namespace
    {
        constexpr std::int32_t HOUSE_COUNT = 8;
        constexpr std::int32_t SLOTS_PER_HOUSE = 3;
        constexpr std::size_t MAX_PENDING = HOUSE_COUNT * SLOTS_PER_HOUSE;
        constexpr std::uint32_t BASE_TTL_FRAMES = 30;

        [[nodiscard]] bool IsValid(BeaconSlot target)
        {
            return target.Owner >= 0 && target.Owner < HOUSE_COUNT &&
                target.Slot >= 0 && target.Slot < SLOTS_PER_HOUSE;
        }

        [[nodiscard]] bool Less(BeaconSlot left, BeaconSlot right)
        {
            return left.Owner < right.Owner ||
                (left.Owner == right.Owner && left.Slot < right.Slot);
        }
    }

    BeaconClearCommandService::BeaconClearCommandService(IBeaconClearGamePort& game)
        : mGame(game)
    {
    }

    void BeaconClearCommandService::OnHotkey()
    {
        if (!SyncSession())
        {
            return;
        }

        const auto rawFrameSendRate = mGame.GetFrameSendRate();
        const auto frameSendRate = rawFrameSendRate <= 0
            ? BASE_TTL_FRAMES
            : static_cast<std::uint32_t>(rawFrameSendRate);
        const auto ttlFrames =
            (static_cast<std::uint64_t>(BASE_TTL_FRAMES - 1) / frameSendRate + 1) *
            frameSendRate;

        auto occupied = mGame.CaptureOccupiedSlots();
        occupied.erase(std::remove_if(occupied.begin(), occupied.end(),
            [](BeaconSlot target) { return !IsValid(target); }), occupied.end());
        std::sort(occupied.begin(), occupied.end(), Less);
        occupied.erase(std::unique(occupied.begin(), occupied.end()), occupied.end());

        for (const auto target : occupied)
        {
            if (mPending.size() == MAX_PENDING)
            {
                break;
            }
            const auto existing = std::find_if(mPending.begin(), mPending.end(),
                [target](const Intent& intent) { return intent.Target == target; });
            if (existing == mPending.end())
            {
                mPending.push_back({target, mLastObservedFrame, ttlFrames});
            }
        }
    }

    void BeaconClearCommandService::OnGameFrame()
    {
        if (!SyncSession() ||
            (mHasAttemptedFrame && mLastAttemptFrame == mLastObservedFrame))
        {
            return;
        }

        while (!mPending.empty())
        {
            auto intent = mPending.front();
            mPending.pop_front();

            // 期限仅在该槽轮到发送时检查；已过期的槽不占用本帧发送机会。
            if (static_cast<std::uint64_t>(mLastObservedFrame - intent.EnqueuedFrame) >=
                intent.TtlFrames)
            {
                continue;
            }

            mHasAttemptedFrame = true;
            mLastAttemptFrame = mLastObservedFrame;
            if (!mGame.TryBroadcastAndDelete(intent.Target.Owner, intent.Target.Slot))
            {
                mPending.push_back(std::move(intent));
            }
            return;
        }
    }

    void BeaconClearCommandService::Reset()
    {
        mPending.clear();
        mSessionIdentity = 0;
        mLastObservedFrame = 0;
        mLastAttemptFrame = 0;
        mHasSession = false;
        mHasAttemptedFrame = false;
    }

    std::size_t BeaconClearCommandService::GetPendingCount() const noexcept
    {
        return mPending.size();
    }

    bool BeaconClearCommandService::SyncSession()
    {
        if (!mGame.IsMatchReady())
        {
            Reset();
            return false;
        }

        const auto sessionIdentity = mGame.GetSessionIdentity();
        if (sessionIdentity == 0)
        {
            Reset();
            return false;
        }

        const auto frame = mGame.GetCurrentFrame();
        if (!mHasSession || mSessionIdentity != sessionIdentity ||
            frame < mLastObservedFrame)
        {
            Reset();
            mHasSession = true;
            mSessionIdentity = sessionIdentity;
        }
        mLastObservedFrame = frame;
        return true;
    }
}
