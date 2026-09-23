#pragma once

#include <string>

namespace ra_commands::game
{
    // YRpp 的固定地址只允许用于 docs/ida-evidence.md 记录的目标样本。
    bool IsSupportedHost(std::string& outError);
}
