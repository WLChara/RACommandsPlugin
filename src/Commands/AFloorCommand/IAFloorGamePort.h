#pragma once

#include <cstdint>

namespace ra_commands::a_floor
{
    /** 仅提供判断本地对局边界所需的值，由游戏侧实现。 */
    class IAFloorGamePort
    {
    public:
        virtual ~IAFloorGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        [[nodiscard]] virtual std::uintptr_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
    };
}
