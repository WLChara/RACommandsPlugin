#pragma once

#include "Commands/AFloorCommand/IAFloorGamePort.h"

namespace ra_commands::game
{
    /** 只读取本地对局身份与帧号；不参与攻击命令的生成。 */
    class AFloorGameAdapter final : public a_floor::IAFloorGamePort
    {
    public:
        [[nodiscard]] bool IsMatchReady() const override;
        [[nodiscard]] std::uintptr_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint32_t GetCurrentFrame() const override;
    };
}
