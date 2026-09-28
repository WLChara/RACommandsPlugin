#include "Commands/AutoFormationCommand/AutoFormationIntentHandler.h"
#include "Commands/AutoFormationCommand/AutoFormationGameAdapter.h"

namespace ra_commands::game
{
    namespace { AutoFormationGameAdapter* g_Adapter = nullptr; }

    void BindAutoFormationGameAdapter(AutoFormationGameAdapter* adapter) noexcept
    {
        g_Adapter = adapter;
    }

    bool ValidateAutoFormationIntent(const commands::ClickedMissionIntent& intent)
    {
        return g_Adapter && g_Adapter->ValidateIntent(intent);
    }

    void AttemptAutoFormationIntent(const commands::ClickedMissionIntent& intent)
    {
        if (g_Adapter) { g_Adapter->AttemptIntent(intent); }
    }
}
