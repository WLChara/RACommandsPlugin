#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using RangeDisplayExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterRangeDisplayCommand(
        const GameSymbols& symbols,
        RangeDisplayExecuteCallback callback,
        std::string& outError);
    void DisableRangeDisplayCommand();
}
