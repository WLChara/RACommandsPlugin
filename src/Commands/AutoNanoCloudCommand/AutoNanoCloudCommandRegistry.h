#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoNanoCloudExecuteCallback = void(*)();

    CommandRegistrationResult TryRegisterAutoNanoCloudCommand(
        const GameSymbols& symbols,
        AutoNanoCloudExecuteCallback callback,
        std::string& outError);
    void DisableAutoNanoCloudCommand();
}
