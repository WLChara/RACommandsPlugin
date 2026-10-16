#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ra_commands::game::chat_profile
{
    // gamemd.exe SHA-256 7cd005d2...：地址及调用后继由本次只读 IDA 指令复核。
    inline constexpr std::uintptr_t PROCESS_INPUT = 0x55E420;
    inline constexpr std::uintptr_t PRINT_MESSAGE = 0x5D3BA0;
    inline constexpr std::uintptr_t CONCAT_MESSAGE = 0x5D3FC0;
    inline constexpr std::uintptr_t MESSAGE_LIST = 0xA8BC60;
    inline constexpr std::uintptr_t SESSION_MODE = 0xA8B238;
    inline constexpr std::uintptr_t ROUTE_SELECTOR = 0xABCE18;
    inline constexpr std::uintptr_t PENDING_CHARACTER = 0xB730EC;
    inline constexpr std::uintptr_t IME_TEXT = 0xB73318;
    inline constexpr std::uintptr_t IME_GATE = 0xB7356C;
    inline constexpr std::uintptr_t KEYBOARD_INSTANCE = 0x87F770;
    inline constexpr std::uintptr_t PLAYER_COLOR = 0xA8B394;
    inline constexpr std::uintptr_t LOCAL_PLAYER = 0xA83D4C;
    inline constexpr std::size_t HOUSE_COLOR_SCHEME_OFFSET = 0x16054;
    inline constexpr std::uintptr_t COLOR_SCHEME_COUNT = 0xB054E0;
    inline constexpr std::uintptr_t BEACON_MANAGER = 0x89C3B0;
    inline constexpr std::uintptr_t FIND_BEACON = 0x431170;
    inline constexpr std::uintptr_t TRANSLATE_KEY = 0x54F450;
    inline constexpr std::uintptr_t ADD_EDIT = 0x5D4210;
    inline constexpr std::uintptr_t REMOVE_EDIT = 0x5D4390;
    inline constexpr std::uintptr_t FONT_INSTANCE = 0x89C4D0;
    inline constexpr std::uintptr_t TEXT_WIDTH = 0x433ED0;
    inline constexpr std::uintptr_t RECEIVE_RETURN = 0x48D97E;
    inline constexpr std::uintptr_t ECHO_RETURN = 0x55F0FA;
    inline constexpr std::uintptr_t LEGACY_CONCAT_RETURN = 0x64A6A6;

    inline constexpr std::array<std::uint8_t, 12> INPUT_ENTRY = { 0x81,0xEC,0x90,0x08,0,0,0x53,0x55,0x56,0x57,0x8B,0xE9 };
    inline constexpr std::array<std::uint8_t, 12> PRINT_ENTRY = { 0x81,0xEC,0x4C,0x01,0,0,0x53,0x55,0x56,0x8B,0xF1,0x8B };
    inline constexpr std::array<std::uint8_t, 12> CONCAT_ENTRY = { 0x51,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x18,0x8B,0xD9,0x85 };
    inline constexpr std::array<std::uint8_t, 12> ADD_EDIT_ENTRY = { 0x56,0x8B,0xF1,0x8A,0x46,0x19,0x84,0xC0,0x74,0x0E,0x8B,0x4E };
    inline constexpr std::array<std::uint8_t, 12> REMOVE_EDIT_ENTRY = { 0x56,0x8B,0xF1,0xE8,0x08,0x3E,0x1A,0,0x8A,0x46,0x19,0x84 };
    inline constexpr std::array<std::uint8_t, 12> TRANSLATE_ENTRY = { 0x83,0xEC,0x0C,0x55,0x56,0x8B,0x74,0x24,0x18,0x8B,0xE9,0xF7 };
    inline constexpr std::array<std::uint8_t, 12> WIDTH_ENTRY = { 0x8B,0x44,0x24,0x08,0x8D,0x54,0x24,0x08,0x50,0x8B,0x44,0x24 };
    inline constexpr std::array<std::uint8_t, 12> BEACON_ENTRY = { 0x8B,0x41,0x60,0x53,0x55,0x56,0x85,0xC0,0x57,0x74,0x38,0x8B };

    struct EntryProbe
    {
        const char* mName;
        std::uintptr_t mAddress;
        std::array<std::uint8_t, 12> mExpected;
        std::size_t mSize;
    };

    constexpr EntryProbe MakeCallProbe(const char* name, std::uintptr_t returnAddress, std::uintptr_t target)
    {
        const auto relative = static_cast<std::uint32_t>(target - returnAddress);
        return { name, returnAddress - 5, { 0xE8, static_cast<std::uint8_t>(relative),
            static_cast<std::uint8_t>(relative >> 8), static_cast<std::uint8_t>(relative >> 16),
            static_cast<std::uint8_t>(relative >> 24) }, 5 };
    }

    inline constexpr std::array<EntryProbe, 11> ENTRY_PROBES = {{
        { "ProcessInput", PROCESS_INPUT, INPUT_ENTRY, 12 },
        { "PrintMessage", PRINT_MESSAGE, PRINT_ENTRY, 12 },
        { "ConcatMessage", CONCAT_MESSAGE, CONCAT_ENTRY, 12 },
        { "AddEdit", ADD_EDIT, ADD_EDIT_ENTRY, 12 },
        { "RemoveEdit", REMOVE_EDIT, REMOVE_EDIT_ENTRY, 12 },
        { "TranslateKey", TRANSLATE_KEY, TRANSLATE_ENTRY, 12 },
        { "TextWidth", TEXT_WIDTH, WIDTH_ENTRY, 12 },
        { "FindBeaconEditor", FIND_BEACON, BEACON_ENTRY, 12 },
        MakeCallProbe("ReceivePrintCall", RECEIVE_RETURN, PRINT_MESSAGE),
        MakeCallProbe("EchoPrintCall", ECHO_RETURN, PRINT_MESSAGE),
        MakeCallProbe("LegacyConcatCall", LEGACY_CONCAT_RETURN, CONCAT_MESSAGE)
    }};

    /** 本地编辑直接使用 House 已解析的色板索引。430CF9/430D0B 的原生通知采用同一字段；
     * AddEdit 5D430B 将该参数原样交给 TextLabel。无需依赖已被 Ares 接管的颜色转换叶函数。
     * 只提供只读内存和创建编辑标签两个能力，不能在此触发颜色映射或网络发送。
     */
    template<typename TReader, typename TAddEdit>
    bool OpenLocalEditor(TReader&& reader, TAddEdit&& addEdit)
    {
        std::uint32_t player = 0;
        int colorScheme = -1;
        int count = 0;
        if (!reader(LOCAL_PLAYER, &player, sizeof(player)) || !player ||
            !reader(static_cast<std::uintptr_t>(player) + HOUSE_COLOR_SCHEME_OFFSET, &colorScheme, sizeof(colorScheme)) ||
            !reader(COLOR_SCHEME_COUNT, &count, sizeof(count)) || colorScheme < 0 || colorScheme >= count) return false;
        return addEdit(colorScheme, L"");
    }

    /** 所有探针均只读；失败逐项报告，不把未知 detour 当作兼容。供适配器和离线内存夹具共用。 */
    template<typename TReader, typename TReport>
    bool ValidateProfile(TReader&& reader, TReport&& report)
    {
        bool matches = true;
        for (const auto& probe : ENTRY_PROBES)
        {
            std::array<std::uint8_t, 12> actual{};
            const bool readable = reader(probe.mAddress, actual.data(), probe.mSize);
            if (!readable || !std::equal(probe.mExpected.begin(), probe.mExpected.begin() + probe.mSize, actual.begin()))
            {
                matches = false;
                report(probe, actual, readable);
            }
        }
        return matches;
    }

    constexpr bool IsRegularPrint(std::uintptr_t returnAddress) noexcept { return returnAddress == RECEIVE_RETURN || returnAddress == ECHO_RETURN; }
    constexpr bool IsRegularConcat(std::uintptr_t returnAddress) noexcept { return returnAddress == LEGACY_CONCAT_RETURN; }
}
