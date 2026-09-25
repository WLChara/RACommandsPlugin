#pragma once

#include "Commands/AutoSuperWeapon/IAutoSuperWeaponGamePort.h"

namespace ra_commands::game
{
    class NativeNetworkEventAdapter;

    class AutoSuperWeaponGameAdapter final
        : public auto_super_weapon::IAutoSuperWeaponGamePort
    {
    public:
        explicit AutoSuperWeaponGameAdapter(NativeNetworkEventAdapter& events);

        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t SessionIdentity() const override;
        [[nodiscard]] std::uint32_t CurrentFrame() const override;
        [[nodiscard]] bool IsWeaponReady(
            auto_super_weapon::Kind kind) const override;
        [[nodiscard]] bool CaptureSnapshot(auto_super_weapon::Kind kind,
            auto_super_weapon::Snapshot& outSnapshot) const override;
        [[nodiscard]] bool TryFireAt(auto_super_weapon::Kind kind,
            auto_super_weapon::Cell center) const override;

    private:
        NativeNetworkEventAdapter& mEvents;
    };
}
