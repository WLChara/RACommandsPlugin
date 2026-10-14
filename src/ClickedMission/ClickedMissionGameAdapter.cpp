#include "ClickedMission/ClickedMissionGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"
#include "Game/PluginOrderScope.h"
#include "NetworkEvent/CommandCapacity.h"

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
        return command_capacity::GetEffectiveClickedMissionFreeSlots();
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
        // 原生下令会同步进入命令 Hook；不能将插件自己的调用作为人工取消通知。
        const PluginOrderScope orderScope({static_cast<std::uint8_t>(intent.Producer), intent.Epoch});
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

    std::size_t ClickedMissionGameAdapter::CancelSubmittedByProducer(
        commands::ClickedMissionProducer producer) const
    {
        return command_capacity::CancelPendingProducer(static_cast<std::uint8_t>(producer));
    }

    std::size_t ClickedMissionGameAdapter::CancelSubmittedByProducerAndActor(
        commands::ClickedMissionProducer producer, const commands::ClickedMissionIdentity& actor) const
    {
        // NetID::Pack 对普通 Abstract 对象使用统一目标标记 52，与 WhatAmI 的兵种值不同。
        return command_capacity::CancelPendingProducerActor(
            static_cast<std::uint8_t>(producer), actor.UniqueId, 52);
    }

    bool ClickedMissionGameAdapter::HasSubmittedActor(commands::ClickedMissionProducer producer,
        std::uintptr_t, std::uint32_t uniqueId, std::uint32_t epoch) const
    {
        return command_capacity::HasPendingProducerActor(
            static_cast<std::uint8_t>(producer), uniqueId, 52, epoch);
    }
}
