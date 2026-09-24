#include "Commands/AutoRepairCommand/AutoRepairCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoRepairCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AutoRepairCommandRegistry(AutoRepairExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAutoRepairAllBuildings",
                    L"自动修理全部建筑物",
                    L"插件",
                    L"开启或关闭自动修理本方受损建筑物"
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
            AutoRepairExecuteCallback mCallback;
        };

        // 原生命令表在整个进程存续期持有对象。
        AutoRepairCommandRegistry* g_AutoRepairCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoRepairCommand(
        const GameSymbols& symbols,
        AutoRepairExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto-repair command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AutoRepairCommandRegistry)
        {
            g_AutoRepairCommandRegistry = new AutoRepairCommandRegistry(callback);
        }
        return g_AutoRepairCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAutoRepairCommand()
    {
        if (g_AutoRepairCommandRegistry)
        {
            g_AutoRepairCommandRegistry->Disable();
        }
    }
}
