#include "Commands/RangeDisplayCommand/RangeDisplayGameAdapter.h"

#include "Game/GameObjectAccess.h"

#include <HouseClass.h>

namespace ra_commands::game
{
    bool RangeDisplayGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady();
    }

    std::uintptr_t RangeDisplayGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t RangeDisplayGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }
}
