#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

namespace ra_commands::game
{
    [[nodiscard]] bool ValidateTeslaChargeIntent(
        const commands::ClickedMissionIntent& intent);
    void AttemptTeslaChargeIntent(const commands::ClickedMissionIntent& intent);
}
