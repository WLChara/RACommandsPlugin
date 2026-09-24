#pragma once

#include <cstdint>

namespace ra_commands::range_display
{
    /** 供纯状态服务识别对局边界的最小游戏侧接口。 */
    class IRangeDisplayGamePort
    {
    public:
        virtual ~IRangeDisplayGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        [[nodiscard]] virtual std::uintptr_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
    };
}
