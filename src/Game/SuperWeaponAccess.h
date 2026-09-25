#pragma once

#include <cstdint>
#include <optional>

class HouseClass;
class SuperClass;

namespace ra_commands::game
{
    struct LocalSuperWeaponSlot
    {
        std::uint32_t Index = 0;
        SuperClass* Weapon = nullptr;
    };

    /** 仅在目标版本校验通过后的游戏线程调用。 */
    [[nodiscard]] bool IsReadyLocalSuperWeapon(
        const SuperClass* weapon, const HouseClass* local);
    [[nodiscard]] std::optional<LocalSuperWeaponSlot> FindReadyLocalSuperWeapon(
        const HouseClass* local, const char* registeredName);
}
