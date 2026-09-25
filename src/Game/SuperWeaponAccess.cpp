#include "Game/SuperWeaponAccess.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <SuperClass.h>

#include <cstring>

namespace ra_commands::game
{
    namespace
    {
        constexpr int MAX_SANE_SUPER_COUNT = 4096;
    }

    bool IsReadyLocalSuperWeapon(
        const SuperClass* weapon, const HouseClass* local)
    {
        return weapon && local && weapon->Owner == local && weapon->Type &&
            weapon->Granted && weapon->IsCharged && !weapon->IsOnHold &&
            weapon->CanFire();
    }

    std::optional<LocalSuperWeaponSlot> FindReadyLocalSuperWeapon(
        const HouseClass* local, const char* registeredName)
    {
        if (!local || !registeredName || !registeredName[0])
        {
            return std::nullopt;
        }
        const auto& supers = local->Supers;
        if (!supers.IsInitialized || supers.Count < 0 ||
            supers.Count > MAX_SANE_SUPER_COUNT ||
            supers.Count > supers.Capacity ||
            (supers.Count > 0 && !supers.Items))
        {
            return std::nullopt;
        }
        for (int index = 0; index < supers.Count; ++index)
        {
            auto* const weapon = supers.Items[index];
            if (!IsReadyLocalSuperWeapon(weapon, local))
            {
                continue;
            }
            const char* const id = weapon->Type->get_ID();
            if (id && std::strcmp(id, registeredName) == 0)
            {
                return LocalSuperWeaponSlot{static_cast<std::uint32_t>(index), weapon};
            }
        }
        return std::nullopt;
    }
}
