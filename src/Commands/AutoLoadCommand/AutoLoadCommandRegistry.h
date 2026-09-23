#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <string>

namespace ra_commands::game
{
    class GameSymbols;
    using AutoLoadExecuteCallback = void(*)();

    // 子类及其进程存续所有权留在 Game 层，不向 Bootstrap 暴露 YRpp 类型。
    CommandRegistrationResult TryRegisterAutoLoadCommand(
        const GameSymbols& symbols,
        AutoLoadExecuteCallback callback,
        std::string& outError);
    void DisableAutoLoadCommand();
}
