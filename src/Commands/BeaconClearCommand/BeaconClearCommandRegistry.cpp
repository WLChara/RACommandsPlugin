#include "Commands/BeaconClearCommand/BeaconClearCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class BeaconClearCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit BeaconClearCommandRegistry(BeaconClearExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAClearAllPlayerBeacons",
                    L"清除所有信标",
                    L"插件",
                    L"尝试清除全场所有玩家信标；需联机验证"
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
            BeaconClearExecuteCallback mCallback;
        };

        // 原生命令表会在整个进程存续期持有对象。
        BeaconClearCommandRegistry* g_BeaconClearCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterBeaconClearCommand(
        const GameSymbols& symbols,
        BeaconClearExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "beacon-clear command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_BeaconClearCommandRegistry)
        {
            g_BeaconClearCommandRegistry = new BeaconClearCommandRegistry(callback);
        }
        return g_BeaconClearCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableBeaconClearCommand()
    {
        if (g_BeaconClearCommandRegistry)
        {
            g_BeaconClearCommandRegistry->Disable();
        }
    }
}
