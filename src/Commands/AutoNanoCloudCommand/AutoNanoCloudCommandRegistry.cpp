#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoNanoCloudCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AutoNanoCloudCommandRegistry(AutoNanoCloudExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAutoNanoCloud",
                    L"焚风步兵击杀回血",
                    L"插件",
                    L"命令选中的猎杀者击杀低价值友方步兵以制造纳米云"
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
            AutoNanoCloudExecuteCallback mCallback;
        };

        // 游戏命令表在进程存续期间持有此对象。
        AutoNanoCloudCommandRegistry* g_AutoNanoCloudCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoNanoCloudCommand(
        const GameSymbols& symbols,
        AutoNanoCloudExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto-nano-cloud command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AutoNanoCloudCommandRegistry)
        {
            g_AutoNanoCloudCommandRegistry = new AutoNanoCloudCommandRegistry(callback);
        }
        return g_AutoNanoCloudCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAutoNanoCloudCommand()
    {
        if (g_AutoNanoCloudCommandRegistry)
        {
            g_AutoNanoCloudCommandRegistry->Disable();
        }
    }
}
