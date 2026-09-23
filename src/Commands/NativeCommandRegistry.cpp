#include "Commands/NativeCommandRegistry.h"

#include "Game/GameSymbols.h"

#include <Windows.h>

#include <cstring>

namespace ra_commands::game
{
    namespace
    {
        // 仅拦截明显损坏的命令数组；并非游戏命令数量的业务上限。
        constexpr int MAX_SANE_COMMAND_CAPACITY = 4096;
    }

    NativeCommandRegistry::NativeCommandRegistry(NativeCommandMetadata metadata)
        : mMetadata(metadata)
    {
    }

    CommandRegistrationResult NativeCommandRegistry::TryRegister(
        const GameSymbols& symbols,
        std::string& outError)
    {
        if (!mMetadata.InternalName || !mMetadata.UiName ||
            !mMetadata.UiCategory || !mMetadata.UiDescription)
        {
            outError = "native command metadata is incomplete";
            return CommandRegistrationResult::Failed;
        }
        if (mIsRegistered)
        {
            mIsEnabled.store(true, std::memory_order_release);
            return CommandRegistrationResult::Registered;
        }

        auto* const commands = CommandClass::Array.get();
        if (!symbols.MatchesCommandArrayAddress(
                reinterpret_cast<std::uintptr_t>(commands)))
        {
            outError = "YRpp command array differs from resolved address";
            return CommandRegistrationResult::Failed;
        }
        if (!commands->IsInitialized || commands->Count <= 0 ||
            commands->Count > commands->Capacity ||
            commands->Capacity > MAX_SANE_COMMAND_CAPACITY ||
            commands->Items == nullptr)
        {
            return CommandRegistrationResult::Pending;
        }

        for (int index = 0; index < commands->Count; ++index)
        {
            const auto* const command = commands->Items[index];
            if (command == this)
            {
                mIsRegistered = true;
                mIsEnabled.store(true, std::memory_order_release);
                outError.clear();
                return CommandRegistrationResult::Registered;
            }
            const char* const name = command ? command->GetName() : nullptr;
            if (name && std::strcmp(name, GetName()) == 0)
            {
                outError = std::string(GetName()) + " is already registered by another module";
                return CommandRegistrationResult::NameConflict;
            }
        }

        if (!commands->AddItem(this))
        {
            outError = std::string("cannot append ") + GetName() + " to native command array";
            return CommandRegistrationResult::Failed;
        }

        mIsRegistered = true;
        mIsEnabled.store(true, std::memory_order_release);
        outError.clear();
        return CommandRegistrationResult::Registered;
    }

    void NativeCommandRegistry::Disable() noexcept
    {
        mIsEnabled.store(false, std::memory_order_release);
    }

    bool NativeCommandRegistry::IsRegistered() const noexcept
    {
        return mIsRegistered;
    }

    const char* NativeCommandRegistry::GetName() const
    {
        return mMetadata.InternalName;
    }

    const wchar_t* NativeCommandRegistry::GetUIName() const
    {
        return mMetadata.UiName;
    }

    const wchar_t* NativeCommandRegistry::GetUICategory() const
    {
        return mMetadata.UiCategory;
    }

    const wchar_t* NativeCommandRegistry::GetUIDescription() const
    {
        return mMetadata.UiDescription;
    }

    void NativeCommandRegistry::Execute(DWORD argument) const
    {
        if (!mIsEnabled.load(std::memory_order_acquire))
        {
            return;
        }
        try
        {
            OnExecute(argument);
        }
        catch (...)
        {
            OutputDebugStringA("[RACommandsPlugin] native command callback failed\n");
        }
    }
}
