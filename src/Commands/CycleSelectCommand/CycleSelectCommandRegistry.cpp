#include "Commands/CycleSelectCommand/CycleSelectCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class CycleSelectCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit CycleSelectCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RACycleSelect",
                    L"逐个选择单位",
                    L"插件",
                    L"每次按键依次选择下一个符合条件的单位"
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
        CycleSelectCommandRegistry* g_CycleSelectCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterCycleSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "cycle selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_CycleSelectCommandRegistry)
        {
            g_CycleSelectCommandRegistry = new CycleSelectCommandRegistry(callback);
        }
        return g_CycleSelectCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableCycleSelectCommand()
    {
        if (g_CycleSelectCommandRegistry)
        {
            g_CycleSelectCommandRegistry->Disable();
        }
    }
}
