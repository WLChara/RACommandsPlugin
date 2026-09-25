#pragma once

#include "Commands/AutoBuild/IAutoBuildGamePort.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ra_commands::auto_build
{
    enum class BuildPhase
    {
        Off,
        Armed,
        Building,
        Ready,
        Queued
    };

    /** 两条建造栏各有独立任务；仅在游戏线程更新。 */
    class AutoBuildCommandService
    {
    public:
        explicit AutoBuildCommandService(IAutoBuildGamePort& game);

        [[nodiscard]] bool OnHotkey(BuildSlot slot);
        void OnGameFrame();
        void Reset();

        [[nodiscard]] BuildPhase GetPhase(BuildSlot slot) const noexcept;

    private:
        struct SlotState
        {
            BuildPhase Phase = BuildPhase::Off;
            std::optional<ProductId> Target;
            std::optional<std::uint32_t> LastEnqueueFrame;
            std::uint32_t QueuedAtFrame = 0;
        };

        [[nodiscard]] bool SyncSession();
        void ProcessSlot(BuildSlot slot, SlotState& state,
            const SlotSnapshot& snapshot, std::uint32_t frame);
        [[nodiscard]] static bool CanTrack(BuildSlot slot,
            const SlotSnapshot& snapshot);
        [[nodiscard]] static std::size_t Index(BuildSlot slot) noexcept;

        IAutoBuildGamePort& mGame;
        std::array<SlotState, 2> mSlots{};
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastObservedFrame = 0;
        bool mHasSession = false;
    };
}
