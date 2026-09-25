#include "Commands/ForceShieldCommand/ForceShieldGameAdapter.h"

#include "Game/CursorCellAccess.h"
#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"
#include "NetworkEvent/NativeNetworkEventAdapter.h"
#include "NetworkEvent/NetworkEvent.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <SuperClass.h>

#include <cstdint>
#include <cstring>
#include <limits>

namespace ra_commands::game
{
    namespace
    {
        constexpr std::uint32_t MIN_NATIVE_FREE_SLOTS = 13;
        constexpr std::uint32_t REPEAT_GUARD_FRAMES = 120;
        constexpr int MAX_SANE_SUPER_COUNT = 4096;
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

        const auto& supers = local->Supers;
        if (!supers.IsInitialized || supers.Count < 0 ||
            supers.Count > MAX_SANE_SUPER_COUNT ||
            supers.Count > supers.Capacity ||
            (supers.Count > 0 && !supers.Items))
        {
            return false;
        }
        for (int index = 0; index < supers.Count; ++index)
        {
            auto* const super = supers.Items[index];
            const char* const id = super && super->Type
                ? super->Type->get_ID() : nullptr;
            if (!id || std::strcmp(id, FORCE_SHIELD_ID) != 0 ||
                super->Owner != local || !super->Granted ||
                !super->IsCharged || super->IsOnHold || !super->CanFire())
            {
                continue;
            }
            const auto frame = GetCurrentGameFrame();
            if (mLastQueuedFrame &&
                mLastHouse == reinterpret_cast<std::uintptr_t>(local) &&
                mLastSuperIndex == static_cast<std::uint32_t>(index) &&
                frame >= *mLastQueuedFrame &&
                frame - *mLastQueuedFrame < REPEAT_GUARD_FRAMES)
            {
                return false;
            }
            const auto event = network_event::BuildSpecialPlaceEvent(
                static_cast<std::uint8_t>(local->ArrayIndex),
                static_cast<std::uint32_t>(index),
                static_cast<std::int16_t>(cell->X),
                static_cast<std::int16_t>(cell->Y));
            if (!mEvents.TryEnqueueLocal(event))
            {
                return false;
            }
            mLastHouse = reinterpret_cast<std::uintptr_t>(local);
            mLastSuperIndex = static_cast<std::uint32_t>(index);
            mLastQueuedFrame = frame;
            return true;
        }
        return false;
    }

    void ForceShieldGameAdapter::Reset() noexcept
    {
        mLastHouse = 0;
        mLastSuperIndex = 0;
        mLastQueuedFrame.reset();
    }
}
