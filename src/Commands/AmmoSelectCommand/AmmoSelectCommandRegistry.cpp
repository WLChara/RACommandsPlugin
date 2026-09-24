#include "Commands/AmmoSelectCommand/AmmoSelectCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AmmoSelectCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AmmoSelectCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RAAmmoSelect",
                    L"按弹药筛选",
                    L"插件",
                    L"每次按键轮换筛选满弹、未满弹与无弹药单位"
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
            void(*mCallback)();
        };

        // 游戏命令表与热键表会持有 this，因此实例保持到进程退出。
        AmmoSelectCommandRegistry* g_AmmoSelectCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAmmoSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "ammo selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AmmoSelectCommandRegistry)
        {
            g_AmmoSelectCommandRegistry = new AmmoSelectCommandRegistry(callback);
        }
        return g_AmmoSelectCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAmmoSelectCommand()
    {
        if (g_AmmoSelectCommandRegistry)
        {
            g_AmmoSelectCommandRegistry->Disable();
        }
    }
}
