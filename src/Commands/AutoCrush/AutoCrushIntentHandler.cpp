#include "Commands/AutoCrush/AutoCrushIntentHandler.h"

#include "Commands/AutoCrush/AutoCrushGameAdapter.h"

#include <YRPPCore.h>
#include <FootClass.h>
#include <CellClass.h>
#include <TechnoClass.h>

namespace ra_commands::game
{
    bool ValidateAutoCrushIntent(const commands::ClickedMissionIntent& intent)
    {
        TechnoClass* actor = nullptr;
        return ResolveAutoCrushMoveCell(intent, actor) != nullptr;
    }

    void AttemptAutoCrushIntent(const commands::ClickedMissionIntent& intent)
    {
        TechnoClass* actor = nullptr;
        auto* const cell = ResolveAutoCrushMoveCell(intent, actor);
        if (cell)
        {
            actor->ClickedMission(Mission::Move, nullptr, cell, nullptr);
        }
    }
}
