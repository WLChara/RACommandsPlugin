#include "Game/ChatCommandGameAdapter.h"
#include "Game/TargetProfiles/ChatCommandProfile.h"
#include "Memory/ProcessMemory.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <random>
#include <utility>

namespace ra_commands::game
{
    namespace
    {
        namespace profile = chat_command_profile;
        struct PlayerSnapshot
        {
            chat_command::Player mPlayer;
            profile::WireIdentity mWire;
            std::uintptr_t mHouse = 0;
        };

        template<typename T>
        bool Read(std::uintptr_t address, T& output) { return memory::TryReadMemory(address, &output, sizeof(output)); }

        bool ReadName(std::uintptr_t address, std::size_t limit, std::wstring& output)
        {
            output.clear();
            for (std::size_t index = 0; index < limit; ++index)
            {
                wchar_t character = 0;
                if (!Read(address + index * sizeof(wchar_t), character)) return false;
                if (!character) return true;
                output += character;
            }
            return false;
        }

        std::uintptr_t LocalHouse()
        {
            std::uint32_t house = 0;
            Read(profile::LOCAL_PLAYER, house);
            return house;
        }

        std::vector<PlayerSnapshot> CapturePlayers(bool* outComplete = nullptr)
        {
            bool complete = true;
            std::vector<PlayerSnapshot> players;
            const auto local = LocalHouse();
            int localId = -1, mode = -1, count = 0, houseCount = 0;
            std::uint32_t nodes = 0, houses = 0;
            if (!local || !Read(local + profile::HOUSE_INDEX, localId) || !Read(profile::SESSION_MODE, mode))
            { if (outComplete) *outComplete = false; return players; }
            if (Read(profile::START_COUNT, count) && count >= 0 && count <= static_cast<int>(profile::MAX_PLAYERS) &&
                Read(profile::START_ITEMS, nodes) && (nodes || !count) && Read(profile::HOUSE_ITEMS, houses) && houses &&
                Read(profile::HOUSE_COUNT, houseCount) && houseCount > 0 && houseCount <= 64)
            {
                for (int index = 0; index < count; ++index)
                {
                    std::uint32_t node = 0, house = 0;
                    int id = -1, color = -1, actualId = -1;
                    std::uint8_t human = 0, controlled = 0;
                    std::wstring name;
                    if (!Read(nodes + sizeof(node) * index, node)) { complete = false; continue; }
                    if (!node) continue;
                    if (!Read(node + profile::NODE_HOUSE, id) || id < 0 || id >= houseCount ||
                        !Read(houses + sizeof(house) * id, house) || !house ||
                        !Read(house + profile::HOUSE_INDEX, actualId) || actualId != id ||
                        !Read(house + profile::HOUSE_CURRENT_PLAYER, human) ||
                        !Read(house + profile::HOUSE_PLAYER_CONTROL, controlled)) { complete = false; continue; }
                    if (!(human || (mode == 0 && controlled) || house == local)) continue;
                    if (!Read(node + profile::NODE_COLOR, color) || !ReadName(node, 20, name)) { complete = false; continue; }
                    if (std::any_of(players.begin(), players.end(), [id](const auto& player) { return player.mPlayer.mId == id; }))
                    { complete = false; continue; }
                    const bool isLocal = house == local;
                    using IsAllied = bool(__thiscall*)(const void*, const void*);
                    const bool allied = !isLocal && reinterpret_cast<IsAllied>(profile::IS_ALLIED_WITH)(
                        reinterpret_cast<const void*>(local), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(house)));
                    players.push_back({ {id, name, isLocal, allied}, {id, profile::EncodeWireName(name), color}, house });
                }
            }
            else complete = false;
            // 战役的原生多人节点可以为空；本机身份仍可用于 @s 和本机姓名显示开关。
            if (localId >= 0 && std::none_of(players.begin(), players.end(), [localId](const auto& player) { return player.mPlayer.mId == localId; }))
            {
                std::wstring name;
                if (!ReadName(local + profile::HOUSE_NAME, 21, name) || name.empty()) name = L"玩家";
                int color = -1;
                Read(0xA8B394, color);
                players.push_back({ {localId, name, true, false}, {localId, profile::EncodeWireName(name), color}, local });
            }
            if (outComplete) *outComplete = complete;
            return players;
        }

        bool IsPrivatePathCompatible(bool relay)
        {
            return profile::ValidatePrivatePath(
                [](std::uintptr_t address, void* output, std::size_t size) { return memory::TryReadMemory(address, output, size); }, relay);
        }

        bool HasExclusiveRelayMask(int connectionId, int houseId, const std::vector<PlayerSnapshot>& players)
        {
            std::uint32_t items = 0;
            int count = 0;
            if (!Read(profile::FALLBACK_CANDIDATES + 4, items) || !items ||
                !Read(profile::FALLBACK_CANDIDATES + 16, count) || count < 1 || count > 8) return false;
            std::array<int, 8> ids{};
            std::array<std::uint8_t, 8> masks{};
            for (int index = 0; index < count; ++index)
            {
                std::uint32_t record = 0;
                std::int8_t id = -1;
                if (!Read(items + sizeof(record) * index, record) || !record || !Read(record, id) || !Read(record + 0x28, masks[index])) return false;
                ids[index] = id;
            }
            std::vector<int> receivers;
            for (const auto& player : players) receivers.push_back(player.mPlayer.mId);
            return profile::AreRelayReceiversExclusive(ids.data(), masks.data(), static_cast<std::size_t>(count), connectionId, houseId, receivers);
        }

        bool ValidatePrivateTarget(int playerId, std::wstring& outError)
        {
            int mode = -1, observer = 0;
            std::uint8_t relay = 0, status = 0, listening = 0;
            if (!Read(profile::SESSION_MODE, mode) || (mode != 3 && mode != 4)) { outError = L"当前对局只能向自己发送本地消息。"; return false; }
            if (!Read(profile::OBSERVER, observer) || observer) { outError = L"当前观察者状态不支持定向发送。"; return false; }
            if (!Read(profile::RELAY_MODE, relay) || !IsPrivatePathCompatible(relay != 0))
            { outError = L"当前原生定向发送路径与支持版本不一致，未发送私聊。"; return false; }
            std::uint32_t globalChannel = 0;
            if (!Read(profile::IPX_MANAGER + 4, status) || !Read(profile::IPX_MANAGER + 5, listening) || !status || !listening ||
                !Read(profile::IPX_MANAGER + 0x48, globalChannel) || !globalChannel)
            { outError = L"聊天网络尚未就绪，未发送私聊。"; return false; }
            bool complete = false;
            const auto players = CapturePlayers(&complete);
            if (!complete) { outError = L"玩家列表未能完整核对，未发送私聊。"; return false; }
            const auto target = std::find_if(players.begin(), players.end(), [playerId](const auto& player) { return player.mPlayer.mId == playerId; });
            if (target == players.end() || target->mPlayer.mIsLocal || playerId < 0 || playerId >= 8)
            { outError = L"目标玩家已不在本局或无法定向发送。"; return false; }
            std::wstring houseName;
            if (!ReadName(target->mHouse + profile::HOUSE_NAME, 21, houseName) || houseName != target->mPlayer.mName)
            { outError = L"目标玩家名称状态已变化，请重新选择。"; return false; }
            int connections = 0;
            if (!Read(profile::IPX_MANAGER + 0x44, connections) || connections < 1 || connections > 7)
            { outError = L"目标玩家没有可用连接，未发送私聊。"; return false; }
            std::vector<profile::PeerIdentity> peers;
            std::vector<std::uint32_t> connectionPointers;
            for (int index = 0; index < connections; ++index)
            {
                std::uint32_t connection = 0;
                int id = -1;
                std::wstring name;
                if (!Read(profile::IPX_MANAGER + 0x28 + sizeof(connection) * index, connection) || !connection ||
                    !Read(connection + 0x64, id) || !ReadName(connection + 0x68, 21, name))
                { outError = L"无法核对聊天连接，未发送私聊。"; return false; }
                peers.push_back({id, std::move(name)});
                connectionPointers.push_back(connection);
            }
            const auto peer = profile::FindUniquePeer(peers, houseName);
            if (peer < 0) { outError = L"目标编号与名称没有共同的唯一连接，未发送私聊。"; return false; }
            std::array<std::uint8_t, 10> address{};
            int addressMode = -1;
            if (!memory::TryReadMemory(connectionPointers[static_cast<std::size_t>(peer)] + 0x4C, address.data(), address.size()) ||
                !Read(profile::ADDRESS_MODE, addressMode) || !profile::IsNativeUnicastAddress(address, addressMode))
            { outError = L"目标地址为空、广播或属于不支持的原生地址模式，未发送私聊。"; return false; }
            if (relay && (mode != 4 || !HasExclusiveRelayMask(peers[static_cast<std::size_t>(peer)].mId, playerId, players)))
            { outError = L"当前中继没有可验证的独占接收者掩码，未发送私聊。"; return false; }
            return true;
        }
    }

    chat_command::PlayerRoster CaptureChatCommandPlayers()
    {
        chat_command::PlayerRoster roster;
        for (auto& player : CapturePlayers()) roster.push_back(std::move(player.mPlayer));
        return roster;
    }

    int GetLocalChatPlayerId() noexcept
    {
        const auto house = LocalHouse();
        int id = -1;
        return house && Read(house + profile::HOUSE_INDEX, id) ? id : -1;
    }

    int ResolveIncomingChatPlayer(std::wstring_view wireName, int playerColor)
    {
        bool complete = false;
        const auto players = CapturePlayers(&complete);
        std::vector<profile::WireIdentity> identities;
        for (const auto& player : players) identities.push_back(player.mWire);
        return profile::ResolveWireIdentity(identities, wireName, playerColor, complete);
    }

    std::size_t NextChatCommandRandomChoice()
    {
        static std::mt19937_64 random([]
        {
            LARGE_INTEGER counter{};
            QueryPerformanceCounter(&counter);
            return static_cast<std::uint64_t>(counter.QuadPart) ^ GetTickCount64();
        }());
        return static_cast<std::size_t>(random());
    }

    bool WithPrivateChatRecipient(int playerId, const std::function<bool()>& send, std::wstring& outError)
    {
        outError.clear();
        try
        {
            if (!send || !ValidatePrivateTarget(playerId, outError)) return false;
            const auto sent = profile::WithSelectedRecipient(reinterpret_cast<std::uint8_t*>(profile::RECIPIENT_FLAGS),
                *reinterpret_cast<std::uint32_t*>(profile::ROUTE_SELECTOR), playerId, send);
            if (!sent) outError = L"原生聊天没有接受本次私聊，未改为广播。";
            return sent;
        }
        catch (...) { outError = L"定向发送未完成，原聊天接收者设置已恢复。"; return false; }
    }
}
