#include "Commands/BeaconClearCommand/BeaconDeletePacket.h"

#include <algorithm>

namespace ra_commands::beacon_clear
{
    namespace
    {
        constexpr std::size_t NAME_OFFSET = 4;
        constexpr std::size_t SLOT_OFFSET = 59;
        constexpr std::size_t OWNER_OFFSET = 60;
        constexpr std::size_t MAX_NAME_BYTES = SLOT_OFFSET - NAME_OFFSET - 1;
        constexpr std::uint8_t DELETE_OPCODE = 33;
    }

    std::optional<BeaconDeletePacket> BuildBeaconDeletePacket(
        std::string_view senderName, BeaconSlot target)
    {
        if (target.Owner < 0 || target.Owner >= 8 ||
            target.Slot < 0 || target.Slot >= 3 ||
            senderName.size() > MAX_NAME_BYTES ||
            senderName.find('\0') != std::string_view::npos)
        {
            return std::nullopt;
        }

        BeaconDeletePacket packet{};
        packet[0] = DELETE_OPCODE; // x86 little-endian DWORD opcode 33。
        std::copy(senderName.begin(), senderName.end(), packet.begin() + NAME_OFFSET);
        packet[SLOT_OFFSET] = static_cast<std::uint8_t>(target.Slot);
        packet[OWNER_OFFSET] = static_cast<std::uint8_t>(target.Owner);
        return packet;
    }
}
