#include "Game/GameSymbols.h"

#include "Signature/PeImage.h"
#include "Game/TargetProfiles/CommandSignatures.h"
#include "Signature/SignatureResolver.h"

namespace ra_commands::game
{
    namespace
    {
        // 当前样本中 CommandClass::Array 的 Items 字段位于对象起始地址 +4。
        constexpr std::uintptr_t VECTOR_ITEMS_FIELD_OFFSET = 4;
    }

    bool GameSymbols::Resolve(std::uintptr_t moduleBase, std::string& outError)
    {
        outError.clear();
        mCommandInitialization = 0;
        mKeyboardHotkeyLoader = 0;
        mCommandArray = 0;
        mHotkeyTable = 0;

        std::uintptr_t commandInitialization = 0;
        std::uintptr_t keyboardHotkeyLoader = 0;
        std::uintptr_t arrayItemsAddress = 0;
        std::uintptr_t hotkeyTableAddress = 0;

        const memory::PeImage image(moduleBase);
        if (!signatures::TryResolve(image, signatures::COMMAND_INITIALIZATION,
                commandInitialization, outError) ||
            !signatures::TryResolve(image, signatures::KEYBOARD_HOTKEY_LOADER,
                keyboardHotkeyLoader, outError) ||
            !signatures::TryResolve(image, signatures::COMMAND_ARRAY_ITEMS,
                arrayItemsAddress, outError) ||
            !signatures::TryResolve(image, signatures::HOTKEY_TABLE,
                hotkeyTableAddress, outError))
        {
            return false;
        }

        if (arrayItemsAddress < VECTOR_ITEMS_FIELD_OFFSET ||
            !image.Contains(arrayItemsAddress) ||
            !image.Contains(arrayItemsAddress - VECTOR_ITEMS_FIELD_OFFSET) ||
            !image.Contains(hotkeyTableAddress))
        {
            outError = "resolved global address is outside the game image";
            return false;
        }

        mCommandInitialization = commandInitialization;
        mKeyboardHotkeyLoader = keyboardHotkeyLoader;
        mCommandArray = arrayItemsAddress - VECTOR_ITEMS_FIELD_OFFSET;
        mHotkeyTable = hotkeyTableAddress;
        return true;
    }

    bool GameSymbols::IsReady() const noexcept
    {
        return mCommandInitialization != 0 && mKeyboardHotkeyLoader != 0 &&
            mCommandArray != 0 && mHotkeyTable != 0;
    }

    bool GameSymbols::MatchesCommandArrayAddress(std::uintptr_t address) const noexcept
    {
        return IsReady() && mCommandArray == address;
    }

    bool GameSymbols::ReloadKeyboardHotkeys() const
    {
        if (!IsReady())
        {
            return false;
        }

        using Loader = char(__cdecl*)();
        return reinterpret_cast<Loader>(mKeyboardHotkeyLoader)() != 0;
    }

}
