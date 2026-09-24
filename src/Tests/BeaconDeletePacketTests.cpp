#include "Commands/BeaconClearCommand/BeaconDeletePacket.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
}

void RunBeaconDeletePacketTests()
{
    using ra_commands::beacon_clear::BuildBeaconDeletePacket;

    const auto packet = BuildBeaconDeletePacket("Local", {7, 2});
    Require(packet.has_value() && packet->size() == 61,
        "beacon delete packet must have native 61-byte length");
    Require((*packet)[0] == 33 && (*packet)[1] == 0 &&
        (*packet)[2] == 0 && (*packet)[3] == 0,
        "delete opcode must be little-endian DWORD 33");
    Require((*packet)[4] == 'L' && (*packet)[8] == 'l' && (*packet)[9] == 0 &&
        (*packet)[59] == 2 && (*packet)[60] == 7,
        "sender name, slot and owner must occupy verified native offsets");
    Require(std::all_of(packet->begin() + 9, packet->begin() + 59,
        [](std::uint8_t byte) { return byte == 0; }),
        "unused packet body must stay zeroed");

    Require(BuildBeaconDeletePacket(std::string(54, 'A'), {0, 0}).has_value(),
        "54-byte name must leave a terminator before slot byte");
    Require(!BuildBeaconDeletePacket(std::string(55, 'A'), {0, 0}).has_value(),
        "name must not overwrite slot byte");
    Require(!BuildBeaconDeletePacket(std::string_view("A\0B", 3), {0, 0}).has_value(),
        "embedded NUL must not truncate the transmitted sender name");
    Require(!BuildBeaconDeletePacket("A", {-1, 0}).has_value() &&
        !BuildBeaconDeletePacket("A", {8, 0}).has_value() &&
        !BuildBeaconDeletePacket("A", {0, 3}).has_value(),
        "out-of-range owner or slot must be rejected");
}
