#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

namespace ra_commands::game
{
    [[nodiscard]] bool ValidateAirSpreadMoveIntent(
        const commands::ClickedMissionIntent& intent);
    void AttemptAirSpreadMoveIntent(const commands::ClickedMissionIntent& intent);
}
