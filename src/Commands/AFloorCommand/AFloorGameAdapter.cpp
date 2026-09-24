#include "Commands/AFloorCommand/AFloorGameAdapter.h"

#include "Game/GameObjectAccess.h"

#include <HouseClass.h>

namespace ra_commands::game
{
    bool AFloorGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady();
    }

    std::uintptr_t AFloorGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t AFloorGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }
}
