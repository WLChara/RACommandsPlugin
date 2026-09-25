#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using ForceShieldExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterForceShieldCommand(
        const GameSymbols& symbols, ForceShieldExecuteCallback callback,
        std::string& outError);
    void DisableForceShieldCommand();
}
