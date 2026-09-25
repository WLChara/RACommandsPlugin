#include "Commands/TeslaChargeCommand/TeslaChargeIntentHandler.h"
#include "Commands/TeslaChargeCommand/TeslaChargeGameRules.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <TechnoClass.h>

namespace ra_commands::game
{
    namespace
    {
        bool IsSupportedTeslaChargeIntent(const commands::ClickedMissionIntent& intent)
        {
            return intent.Producer == commands::ClickedMissionProducer::TeslaCharge &&
                intent.Mission == static_cast<std::int32_t>(Mission::Attack) &&
                intent.Target && !intent.TargetCell && !intent.Nearest &&
                !intent.DestinationCell && intent.Actor.Epoch == intent.Epoch &&
                intent.Target->Epoch == intent.Epoch;
        }
    }

    bool ValidateTeslaChargeIntent(const commands::ClickedMissionIntent& intent)
    {
        return IsSupportedTeslaChargeIntent(intent) &&
            CanChargeTesla(ResolveIdentity(intent.Actor), ResolveIdentity(*intent.Target));
    }

    void AttemptTeslaChargeIntent(const commands::ClickedMissionIntent& intent)
    {
        if (!IsSupportedTeslaChargeIntent(intent))
        {
            return;
        }
        auto* const charger = ResolveIdentity(intent.Actor);
        auto* const tesla = ResolveIdentity(*intent.Target);
        if (CanChargeTesla(charger, tesla))
        {
            charger->ClickedMission(Mission::Attack, tesla, nullptr, nullptr);
        }
    }
}
