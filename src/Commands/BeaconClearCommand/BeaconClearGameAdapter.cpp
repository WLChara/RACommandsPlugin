#include "Commands/BeaconClearCommand/BeaconClearGameAdapter.h"
#include "Commands/BeaconClearCommand/BeaconDeletePacket.h"

#include "Game/GameObjectAccess.h"
#include "Memory/ProcessMemory.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <SessionClass.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

namespace ra_commands::game
{
    namespace
    {
        // 以下地址仅适用于 TargetVersion 的 Mental Omega EXE 样本。
        constexpr std::uintptr_t BEACON_MANAGER_ADDRESS = 0x0089C3B0u;
        constexpr std::uintptr_t TRANSPORT_ADDRESS = 0x00A8E9C0u;
        constexpr std::uintptr_t PLAYER_TABLE_ADDRESS = 0x00A8DA78u;
        constexpr std::uintptr_t PLAYER_COUNT_ADDRESS = 0x00A8DA84u;
        constexpr std::uintptr_t NETWORK_INTERFACE_ADDRESS = 0x00887628u;
        constexpr std::uintptr_t DELETE_BEACON_ADDRESS = 0x004311C0u;
        constexpr std::uintptr_t ADD_TO_PROGRESS_ADDRESS = 0x005410F0u;
        constexpr std::uintptr_t GET_PLAYER_NAME_ADDRESS = 0x007350C0u;

        constexpr std::size_t HOUSE_COUNT = 8;
        constexpr std::size_t SLOTS_PER_HOUSE = 3;
        constexpr std::size_t BEACON_SLOT_COUNT = HOUSE_COUNT * SLOTS_PER_HOUSE;
        // 传输层记录数未被证明恒等于玩家数；超过此防御性上限则拒绝发送。
        constexpr int MAX_NETWORK_RECORDS = 16;
        constexpr std::size_t NAME_FIELD_BYTES = 55;
        constexpr std::size_t PLAYER_ADDRESS_OFFSET = 40;

        using DeleteBeaconFunction = void(__thiscall*)(void*, int, int);
        using AddToProgressFunction = int(__thiscall*)(
            void*, void*, int, int, void*, int, int);
        using GetPlayerNameFunction = const char*(__thiscall*)(void*);

        [[nodiscard]] bool IsValidSlot(std::int32_t owner, std::int32_t slot)
        {
            return owner >= 0 && owner < static_cast<std::int32_t>(HOUSE_COUNT) &&
                slot >= 0 && slot < static_cast<std::int32_t>(SLOTS_PER_HOUSE);
        }

        [[nodiscard]] std::size_t FlatIndex(std::int32_t owner, std::int32_t slot)
        {
            return static_cast<std::size_t>(owner) * SLOTS_PER_HOUSE +
                static_cast<std::size_t>(slot);
        }

        [[nodiscard]] bool ReadBeaconSlots(
            std::array<std::uintptr_t, BEACON_SLOT_COUNT>& outSlots)
        {
            static_assert(sizeof(void*) == sizeof(std::uint32_t));
            return memory::TryReadMemory(BEACON_MANAGER_ADDRESS,
                outSlots.data(), sizeof(outSlots));
        }

        [[nodiscard]] bool IsLocalOnlySession()
        {
            const auto mode = SessionClass::Instance->GameMode;
            return mode == GameMode::Campaign || mode == GameMode::Skirmish;
        }

        [[nodiscard]] bool IsNetworkSession()
        {
            const auto mode = SessionClass::Instance->GameMode;
            return mode == GameMode::LAN || mode == GameMode::Internet;
        }

        [[nodiscard]] bool ReadActivePlayers(
            std::vector<std::uintptr_t>& outPlayers)
        {
            std::uintptr_t tableAddress = 0;
            int count = 0;
            if (!memory::TryReadMemory(PLAYER_TABLE_ADDRESS,
                    &tableAddress, sizeof(tableAddress)) ||
                !memory::TryReadMemory(PLAYER_COUNT_ADDRESS, &count, sizeof(count)) ||
                !tableAddress || count < 1 || count > MAX_NETWORK_RECORDS)
            {
                return false;
            }
            outPlayers.resize(count);
            if (!memory::TryReadMemory(tableAddress,
                    outPlayers.data(), static_cast<std::size_t>(count) * sizeof(void*)))
            {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool BuildDeletePacket(
            void* localPlayer, std::int32_t owner, std::int32_t slot,
            beacon_clear::BeaconDeletePacket& outPacket)
        {
            if (!localPlayer || !IsValidSlot(owner, slot))
            {
                return false;
            }
            std::uint32_t playerProbe = 0;
            if (!memory::TryReadMemory(
                    reinterpret_cast<std::uintptr_t>(localPlayer),
                    &playerProbe, sizeof(playerProbe)))
            {
                return false;
            }
            const auto getName = reinterpret_cast<GetPlayerNameFunction>(GET_PLAYER_NAME_ADDRESS);
            const char* const name = getName(localPlayer);
            if (!name)
            {
                return false;
            }

            const auto nameLength = strnlen_s(name, NAME_FIELD_BYTES);
            if (nameLength >= NAME_FIELD_BYTES)
            {
                return false;
            }
            const auto packet = beacon_clear::BuildBeaconDeletePacket(
                std::string_view(name, nameLength), {owner, slot});
            if (!packet)
            {
                return false;
            }
            outPacket = *packet;
            return true;
        }
    }

    bool BeaconClearGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady();
    }

    std::uintptr_t BeaconClearGameAdapter::GetSessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t BeaconClearGameAdapter::GetCurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    std::int32_t BeaconClearGameAdapter::GetFrameSendRate() const
    {
        return GetGameFrameSendRate();
    }

    std::vector<beacon_clear::BeaconSlot> BeaconClearGameAdapter::CaptureOccupiedSlots() const
    {
        std::vector<beacon_clear::BeaconSlot> slots;
        if (!IsMatchReady())
        {
            return slots;
        }
        std::array<std::uintptr_t, BEACON_SLOT_COUNT> current{};
        if (!ReadBeaconSlots(current))
        {
            return slots;
        }
        for (std::size_t owner = 0; owner < HOUSE_COUNT; ++owner)
        {
            for (std::size_t slot = 0; slot < SLOTS_PER_HOUSE; ++slot)
            {
                if (current[owner * SLOTS_PER_HOUSE + slot])
                {
                    slots.push_back({static_cast<std::int32_t>(owner),
                        static_cast<std::int32_t>(slot)});
                }
            }
        }
        return slots;
    }

    void BeaconClearGameAdapter::Reset() noexcept
    {
        mProgress.Reset();
        mSessionIdentity = 0;
        mLastFrame = 0;
    }

    void BeaconClearGameAdapter::SyncSessionState()
    {
        if (!IsMatchReady())
        {
            Reset();
            return;
        }
        const auto identity = GetSessionIdentity();
        const auto frame = GetCurrentFrame();
        if (identity != mSessionIdentity || frame < mLastFrame)
        {
            Reset();
            mSessionIdentity = identity;
        }
        mLastFrame = frame;
    }

    bool BeaconClearGameAdapter::TryBroadcastAndDelete(
        std::int32_t owner, std::int32_t slot)
    {
        if (!IsMatchReady())
        {
            Reset();
            return false;
        }
        if (!IsValidSlot(owner, slot))
        {
            return false;
        }
        SyncSessionState();
        std::array<std::uintptr_t, BEACON_SLOT_COUNT> current{};
        if (!ReadBeaconSlots(current))
        {
            return false;
        }
        const auto flatIndex = FlatIndex(owner, slot);
        if (!current[flatIndex])
        {
            mProgress.ClearSlot(flatIndex);
            return true;
        }
        if (!mProgress.Begin(flatIndex, current[flatIndex]))
        {
            // 旧槽已被新信标复用，不重发旧删除命令去误删新信标。
            return true;
        }

        if (IsNetworkSession())
        {
            std::uintptr_t networkInterface = 0;
            if (!memory::TryReadMemory(NETWORK_INTERFACE_ADDRESS,
                    &networkInterface, sizeof(networkInterface)) ||
                !networkInterface)
            {
                return false;
            }

            std::vector<std::uintptr_t> players;
            if (!ReadActivePlayers(players) || !players[0])
            {
                return false;
            }
            beacon_clear::BeaconDeletePacket packet{};
            if (!BuildDeletePacket(reinterpret_cast<void*>(players[0]),
                    owner, slot, packet))
            {
                return false;
            }

            const auto send = reinterpret_cast<AddToProgressFunction>(ADD_TO_PROGRESS_ADDRESS);
            for (std::size_t index = 1; index < players.size(); ++index)
            {
                if (!players[index] ||
                    players[index] > (std::numeric_limits<std::uintptr_t>::max)() -
                        PLAYER_ADDRESS_OFFSET)
                {
                    return false;
                }
                if (mProgress.HasSubmitted(flatIndex, players[index]))
                {
                    continue;
                }
                const auto destination = players[index] + PLAYER_ADDRESS_OFFSET;
                std::array<std::uint8_t, 16> addressProbe{};
                if (!memory::TryReadMemory(destination,
                        addressProbe.data(), addressProbe.size()) ||
                    !send(reinterpret_cast<void*>(TRANSPORT_ADDRESS), packet.data(),
                        static_cast<int>(packet.size()), 1,
                        reinterpret_cast<void*>(destination), 0, 0))
                {
                    return false;
                }
                mProgress.MarkSubmitted(flatIndex, players[index]);
            }
        }
        else if (!IsLocalOnlySession())
        {
            return false;
        }

        const auto remove = reinterpret_cast<DeleteBeaconFunction>(DELETE_BEACON_ADDRESS);
        remove(reinterpret_cast<void*>(BEACON_MANAGER_ADDRESS), owner, slot);
        mProgress.ClearSlot(flatIndex);
        return true;
    }
}
