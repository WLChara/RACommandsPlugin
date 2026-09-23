#include "Commands/AutoLoadCommand/AutoLoadCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoLoadCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AutoLoadCommandRegistry(AutoLoadExecuteCallback callback)
                : NativeCommandRegistry({
                    "YRHMAutoLoad",
                    L"自动装车",
                    L"插件",
                    L"令选中的单位执行一次自动装车"
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
            AutoLoadExecuteCallback mCallback;
        };

        // 游戏命令表与热键表会保留 this；注册后不可在运行中释放对象。
        AutoLoadCommandRegistry* g_AutoLoadCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoLoadCommand(
        const GameSymbols& symbols,
        AutoLoadExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto-load command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AutoLoadCommandRegistry)
        {
            g_AutoLoadCommandRegistry = new AutoLoadCommandRegistry(callback);
        }
        return g_AutoLoadCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAutoLoadCommand()
    {
        if (g_AutoLoadCommandRegistry)
        {
            g_AutoLoadCommandRegistry->Disable();
        }
    }
}
