#pragma once

#include "Commands/AutoSuperWeapon/AutoSuperWeaponPlanner.h"

#include <cstdint>

namespace ra_commands::auto_super_weapon
{
    class IAutoSuperWeaponGamePort
    {
    public:
        virtual ~IAutoSuperWeaponGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        [[nodiscard]] virtual std::uintptr_t SessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t CurrentFrame() const = 0;
        [[nodiscard]] virtual bool IsWeaponReady(Kind kind) const = 0;
        [[nodiscard]] virtual bool CaptureSnapshot(
            Kind kind, Snapshot& outSnapshot) const = 0;
        [[nodiscard]] virtual bool TryFireAt(Kind kind, Cell center) const = 0;
    };
}
