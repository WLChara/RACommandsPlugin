#pragma once

#include "Commands/CommandRegistrationResult.h"

#include <YRPPCore.h>
#include <CommandClass.h>

#include <atomic>
#include <string>

namespace ra_commands::game
{
    class GameSymbols;

    struct NativeCommandMetadata
    {
        const char* InternalName = nullptr;
        const wchar_t* UiName = nullptr;
        const wchar_t* UiCategory = nullptr;
        const wchar_t* UiDescription = nullptr;
    };

    /**
     * 可继承的游戏原生 Command 基类，统一处理注册、元数据及停用。
     * 元数据字符串必须在进程存续期有效；注册后游戏命令表及热键表会持有 this，
     * 因此对象不能在游戏进程结束前销毁。注册只能在已确认的游戏线程调用。
     */
    class NativeCommandRegistry : public CommandClass
    {
    public:
        explicit NativeCommandRegistry(NativeCommandMetadata metadata);
        ~NativeCommandRegistry() override = default;

        CommandRegistrationResult TryRegister(const GameSymbols& symbols, std::string& outError);
        void Disable() noexcept;
        [[nodiscard]] bool IsRegistered() const noexcept;

        const char* GetName() const final;
        const wchar_t* GetUIName() const final;
        const wchar_t* GetUICategory() const final;
        const wchar_t* GetUIDescription() const final;
        void Execute(DWORD argument) const final;

    protected:
        virtual void OnExecute(DWORD argument) const = 0;

    private:
        NativeCommandMetadata mMetadata;
        std::atomic<bool> mIsEnabled{false};
        bool mIsRegistered = false;
    };
}
