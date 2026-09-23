#include "Commands/TeslaChargeCommand/TeslaChargeCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class TeslaChargeCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit TeslaChargeCommandRegistry(TeslaChargeExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAutoTeslaCharge",
                    L"自动为线圈充电",
                    L"插件",
                    L"开启或关闭为磁暴线圈自动充能"
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
            TeslaChargeExecuteCallback mCallback;
        };

        // 原生命令表在整个进程存续期持有对象。
        TeslaChargeCommandRegistry* g_TeslaChargeCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterTeslaChargeCommand(
        const GameSymbols& symbols,
        TeslaChargeExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "tesla-charge command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_TeslaChargeCommandRegistry)
        {
            g_TeslaChargeCommandRegistry = new TeslaChargeCommandRegistry(callback);
        }
        return g_TeslaChargeCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableTeslaChargeCommand()
    {
        if (g_TeslaChargeCommandRegistry)
        {
            g_TeslaChargeCommandRegistry->Disable();
        }
    }
}
