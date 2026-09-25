#include "ClickedMission/ClickedMissionGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"

#include <YRPPCore.h>
#include <HouseClass.h>

namespace ra_commands::game
{
    namespace
    {
        bool IsSupportedEnterIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Producer != commands::ClickedMissionProducer::Count &&
                intent.Mission == static_cast<std::int32_t>(Mission::Enter) &&
                !intent.Target && intent.TargetCell && !intent.Nearest &&
                !intent.DestinationCell &&
                intent.Actor.Epoch == intent.Epoch &&
                intent.TargetCell->Epoch == intent.Epoch;
        }
    }

    bool ClickedMissionGameAdapter::BindIntentHandler(
        commands::ClickedMissionProducer producer,
        commands::ClickedMissionIntentHandler handler) noexcept
    {
        return mHandlers.Bind(producer, handler);
    }

    bool ClickedMissionGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady();
    }

    std::uintptr_t ClickedMissionGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t ClickedMissionGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    std::uint32_t ClickedMissionGameAdapter::GetNativeFreeSlots() const
    {
        return GetNativeEventFreeSlots();
    }

    bool ClickedMissionGameAdapter::ValidateClickedMissionIntent(
        const commands::ClickedMissionIntent& intent) const
    {
        if (const auto* const handler = mHandlers.Find(intent.Producer))
        {
            return handler->Validate(intent);
        }
        if (IsSupportedEnterIntent(intent))
        {
            return CanEnterTransport(ResolveIdentity(intent.Actor),
                ResolveIdentity(*intent.TargetCell));
        }
        return false;
    }

    void ClickedMissionGameAdapter::AttemptClickedMission(
        const commands::ClickedMissionIntent& intent) const
    {
        if (const auto* const handler = mHandlers.Find(intent.Producer))
        {
            handler->Attempt(intent);
            return;
        }
        if (IsSupportedEnterIntent(intent))
        {
            auto* const passenger = ResolveIdentity(intent.Actor);
            auto* const transport = ResolveIdentity(*intent.TargetCell);
            if (CanEnterTransport(passenger, transport))
            {
                passenger->ClickedMission(Mission::Enter, nullptr, transport, nullptr);
            }
            return;
        }
    }
}
