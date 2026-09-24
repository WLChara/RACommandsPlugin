#pragma once

#include "Commands/BeaconClearCommand/IBeaconClearGamePort.h"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace ra_commands::beacon_clear
{
    /** 按值排队的单次清除命令；调用方须在游戏主线程持有 port 和服务。 */
    class BeaconClearCommandService final
    {
    public:
        explicit BeaconClearCommandService(IBeaconClearGamePort& game);

        void OnHotkey();
        void OnGameFrame();
        void Reset();
        [[nodiscard]] std::size_t GetPendingCount() const noexcept;

    private:
        struct Intent
        {
            BeaconSlot Target;
            std::uint32_t EnqueuedFrame = 0;
            std::uint64_t TtlFrames = 0;
        };

        [[nodiscard]] bool SyncSession();

        IBeaconClearGamePort& mGame;
        std::deque<Intent> mPending;
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastObservedFrame = 0;
        std::uint32_t mLastAttemptFrame = 0;
        bool mHasSession = false;
        bool mHasAttemptedFrame = false;
    };
}
