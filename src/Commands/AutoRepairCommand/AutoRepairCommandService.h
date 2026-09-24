#pragma once

#include "Commands/AutoRepairCommand/IAutoRepairGamePort.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace ra_commands::auto_repair
{
    /** 持续修理开关及发送节流；仅由游戏主线程调用。 */
    class AutoRepairCommandService final
    {
    public:
        explicit AutoRepairCommandService(IAutoRepairGamePort& game);

        void OnHotkey();
        void OnGameFrame();
        void Reset();
        [[nodiscard]] bool IsEnabled() const noexcept;

    private:
        struct BuildingIdHash
        {
            [[nodiscard]] std::size_t operator()(BuildingId id) const noexcept
            {
                return std::hash<std::uintptr_t>{}(id.Address) ^
                    (std::hash<std::uint32_t>{}(id.UniqueId) << 1);
            }
        };

        [[nodiscard]] bool SyncSession();

        IAutoRepairGamePort& mGame;
        std::unordered_map<BuildingId, std::uint32_t, BuildingIdHash> mLastAttemptFrames;
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastObservedFrame = 0;
        bool mHasSession = false;
        bool mEnabled = false;
    };
}
