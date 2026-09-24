#pragma once

#include "Commands/BeaconClearCommand/IBeaconClearGamePort.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ra_commands::beacon_clear
{
    constexpr std::size_t BEACON_DELETE_PACKET_BYTES = 61;
    using BeaconDeletePacket = std::array<std::uint8_t, BEACON_DELETE_PACKET_BYTES>;

    /** 编码目标样本的 Global Channel 删信标命令；不负责发送或本地删除。 */
    [[nodiscard]] std::optional<BeaconDeletePacket> BuildBeaconDeletePacket(
        std::string_view senderName, BeaconSlot target);
}
