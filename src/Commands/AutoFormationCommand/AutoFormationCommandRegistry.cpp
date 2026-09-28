#include "Commands/AutoFormationCommand/AutoFormationCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoFormationCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AutoFormationCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RAAutoFormation",
                    L"自动列队",
                    L"插件",
                    L"将选中的本方地面载具向群体中心紧密列队，尽量形成正方形"
                }), mCallback(callback)
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
            void(*mCallback)();
        };

        // 游戏命令表与热键表持有实例，生命周期延续到进程退出。
        AutoFormationCommandRegistry* g_AutoFormationCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoFormationCommand(
        const GameSymbols& symbols, void(*callback)(), std::string& outError)
    {
        if (!callback)
        {
            outError = "auto-formation callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AutoFormationCommandRegistry)
        {
            g_AutoFormationCommandRegistry = new AutoFormationCommandRegistry(callback);
        }
        return g_AutoFormationCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAutoFormationCommand()
    {
        if (g_AutoFormationCommandRegistry)
        {
            g_AutoFormationCommandRegistry->Disable();
        }
    }
}
