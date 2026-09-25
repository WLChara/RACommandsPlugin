#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class SafeModeToggleCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit SafeModeToggleCommandRegistry(SafeModeToggleExecuteCallback callback)
                : NativeCommandRegistry({
                    "RASafeModeToggle",
                    L"切换安全模式",
                    L"插件",
                    L"开启或关闭安全模式，调整部分功能强度"
                }),
                mCallback(callback)
            {
            }

        protected:
            void OnExecute(DWORD) const override
            {
                if (mCallback)
                {
                    mCallback();
                }
            }

        private:
            SafeModeToggleExecuteCallback mCallback;
        };

        // 原生命令表在进程存续期间持有此对象。
        SafeModeToggleCommandRegistry* g_SafeModeToggleCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterSafeModeToggleCommand(
        const GameSymbols& symbols,
        SafeModeToggleExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "safe-mode command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_SafeModeToggleCommandRegistry)
        {
            g_SafeModeToggleCommandRegistry = new SafeModeToggleCommandRegistry(callback);
        }
        return g_SafeModeToggleCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableSafeModeToggleCommand()
    {
        if (g_SafeModeToggleCommandRegistry)
        {
            g_SafeModeToggleCommandRegistry->Disable();
        }
    }
}
