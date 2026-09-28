#pragma once

#include <cstdint>
#include <cmath>

namespace ra_commands::auto_formation
{
    // 目标样本 CellClass 的 +0x124/+0x128 是原生占用字节。
    // SDK 将其暴露为 DWORD，但相邻三个字节不能参加占用判断。
    [[nodiscard]] constexpr std::uint8_t ReadNativeOccupationByte(
        const std::uint8_t* nativeBytes) noexcept
    {
        return nativeBytes[0];
    }

    [[nodiscard]] constexpr bool OccupationAllowsVehicleContents(
        std::uint8_t flags, bool hasVehicleContent) noexcept
    {
        // 低五位为步兵子位置，0x20 为载具，0x40 为物体，0x80 为建筑。
        // 预约了载具位却没有对应车辆内容时，不能把预约当作空地。
        return (flags & 0xDFu) == 0 && ((flags & 0x20u) == 0 || hasVehicleContent);
    }

    [[nodiscard]] constexpr unsigned NativeHeadingBucket(std::uint16_t heading) noexcept
    {
        return (((heading >> 12) + 1u) >> 1) & 7u;
    }

    /** 对应 0x73F8E0..0x73FA26 的移动友军迎面避碰 No 子集。 */
    [[nodiscard]] inline bool IsNativeHeadOnBlock(std::uint16_t actorHeading,
        std::uint16_t occupantHeading, double dx, double dy, double dz,
        bool occupantIsMoving) noexcept
    {
        if (!occupantIsMoving || dx * dx + dy * dy + dz * dz > 511.0 * 511.0 ||
            (dx == 0.0 && dy == 0.0)) { return false; }
        const unsigned actorDirection = NativeHeadingBucket(actorHeading);
        const auto reverseOccupant = static_cast<std::uint16_t>(occupantHeading + 0x7FFFu);
        if (actorDirection != NativeHeadingBucket(reverseOccupant)) { return false; }
        constexpr double PI = 3.14159265358979323846;
        const auto towardOccupant = static_cast<std::uint16_t>(std::lround(
            (std::atan2(-dy, dx) - PI / 2.0) * (-65536.0 / (2.0 * PI))));
        return actorDirection == NativeHeadingBucket(towardOccupant);
    }

    /** 原生 Move 值仅在适配器已完成地形、层、内容和组授权检查后使用。 */
    [[nodiscard]] constexpr bool NativeMoveAllowsGroupDestination(
        int nativeMove, bool hasAuthorizedParticipant, bool knownHeadOnBlock) noexcept
    {
        if (nativeMove == 0) { return true; }
        return hasAuthorizedParticipant &&
            (nativeMove == 2 || nativeMove == 6 || (nativeMove == 7 && knownHeadOnBlock));
    }
}
