#include "Commands/ForceShieldCommand/ForceShieldGameAdapter.h"

#include "Game/CursorCellAccess.h"
#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"
#include "Game/SuperWeaponAccess.h"
#include "NetworkEvent/NativeNetworkEventAdapter.h"
#include "NetworkEvent/NetworkEvent.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <cstdint>
#include <limits>

namespace ra_commands::game
{
    namespace
    {
        constexpr std::uint32_t MIN_NATIVE_FREE_SLOTS = 13;
        constexpr std::uint32_t REPEAT_GUARD_FRAMES = 120;
        constexpr char FORCE_SHIELD_ID[] = "ForceShieldSpecial";
    }

    ForceShieldGameAdapter::ForceShieldGameAdapter(NativeNetworkEventAdapter& events)
        : mEvents(events)
    {
    }

    bool ForceShieldGameAdapter::TryFireAtCursor()
    {
        if (!IsGameSessionReady() ||
            GetNativeEventFreeSlots() < MIN_NATIVE_FREE_SLOTS)
        {
            return false;
        }
        auto* const local = HouseClass::Player.get();
        const auto cell = CaptureCursorCell();
        if (!local || !cell || local->ArrayIndex < 0 ||
            local->ArrayIndex > 255 ||
            cell->X < std::numeric_limits<std::int16_t>::min() ||
            cell->X > std::numeric_limits<std::int16_t>::max() ||
            cell->Y < std::numeric_limits<std::int16_t>::min() ||
            cell->Y > std::numeric_limits<std::int16_t>::max())
        {
            return false;
        }

        const auto ready = FindReadyLocalSuperWeapon(local, FORCE_SHIELD_ID);
        if (!ready)
        {
            return false;
        }
        const auto frame = GetCurrentGameFrame();
        if (mLastQueuedFrame &&
            mLastHouse == reinterpret_cast<std::uintptr_t>(local) &&
            mLastSuperIndex == ready->Index &&
            frame >= *mLastQueuedFrame &&
            frame - *mLastQueuedFrame < REPEAT_GUARD_FRAMES)
        {
            return false;
        }
        const auto event = network_event::BuildSpecialPlaceEvent(
            static_cast<std::uint8_t>(local->ArrayIndex), ready->Index,
            static_cast<std::int16_t>(cell->X),
            static_cast<std::int16_t>(cell->Y));
        if (!mEvents.TryEnqueueLocal(event))
        {
            return false;
        }
        mLastHouse = reinterpret_cast<std::uintptr_t>(local);
        mLastSuperIndex = ready->Index;
        mLastQueuedFrame = frame;
        return true;
    }

    void ForceShieldGameAdapter::Reset() noexcept
    {
        mLastHouse = 0;
        mLastSuperIndex = 0;
        mLastQueuedFrame.reset();
    }
}
