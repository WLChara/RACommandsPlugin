#include "ClickedMission/ClickedMissionGameAdapter.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <Networking.h>

namespace ra_commands::game
{
    namespace
    {
        // 当前目标样本的原生 OutList 物理容量，见 docs/ida-evidence.md。
        constexpr int NATIVE_OUTLIST_CAPACITY = 128;

        bool IsSupportedEnterIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Mission == static_cast<std::int32_t>(Mission::Enter) &&
                !intent.Target && intent.TargetCell && !intent.Nearest &&
                intent.Actor.Epoch == intent.Epoch &&
                intent.TargetCell->Epoch == intent.Epoch;
        }
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
        const int queued = Networking::LastEventIndex();
        return queued >= 0 && queued <= NATIVE_OUTLIST_CAPACITY
            ? static_cast<std::uint32_t>(NATIVE_OUTLIST_CAPACITY - queued)
            : 0;
    }

    bool ClickedMissionGameAdapter::ValidateClickedMissionIntent(
        const commands::ClickedMissionIntent& intent) const
    {
        if (!IsSupportedEnterIntent(intent))
        {
            return false;
        }
        return CanEnterTransport(ResolveIdentity(intent.Actor),
            ResolveIdentity(*intent.TargetCell));
    }

    void ClickedMissionGameAdapter::AttemptClickedMission(
        const commands::ClickedMissionIntent& intent) const
    {
        if (!IsSupportedEnterIntent(intent))
        {
            return;
        }

        auto* const passenger = ResolveIdentity(intent.Actor);
        auto* const transport = ResolveIdentity(*intent.TargetCell);
        if (CanEnterTransport(passenger, transport))
        {
            passenger->ClickedMission(Mission::Enter, nullptr, transport, nullptr);
        }
    }
}
