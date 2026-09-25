#pragma once

#include "Commands/AutoSuperWeapon/IAutoSuperWeaponGamePort.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ra_commands::auto_super_weapon
{
    class AutoSuperWeaponCommandService
    {
    public:
        explicit AutoSuperWeaponCommandService(IAutoSuperWeaponGamePort& game);

        [[nodiscard]] bool OnHotkey(Kind kind);
        void OnGameFrame();
        void Reset() noexcept;
        [[nodiscard]] bool IsEnabled(Kind kind) const noexcept;

    private:
        struct State
        {
            bool Enabled = false;
            bool Pending = false;
            std::optional<std::uint32_t> LastAttemptFrame;
        };

        [[nodiscard]] bool SyncSession();
        void Process(Kind kind, State& state);
        [[nodiscard]] static std::size_t Index(Kind kind) noexcept;

        IAutoSuperWeaponGamePort& mGame;
        std::array<State, 2> mStates{};
        std::uintptr_t mSessionIdentity = 0;
        std::uint32_t mLastFrame = 0;
        bool mHasSession = false;
    };
}
