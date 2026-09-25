#pragma once

#include "ClickedMission/ClickedMissionQueue.h"

namespace ra_commands::game
{
    [[nodiscard]] bool ValidateAutoCrushIntent(
        const commands::ClickedMissionIntent& intent);
    void AttemptAutoCrushIntent(const commands::ClickedMissionIntent& intent);
}
