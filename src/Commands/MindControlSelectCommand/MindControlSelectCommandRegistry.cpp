#include "Commands/MindControlSelectCommand/MindControlSelectCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class MindControlSelectCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit MindControlSelectCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RAMindControlSelect",
                    L"筛选心控单位",
                    L"插件",
                    L"每次按键轮换筛选被心灵控制与未被心灵控制的单位"
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
        MindControlSelectCommandRegistry* g_MindControlSelectCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterMindControlSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "mind-control selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_MindControlSelectCommandRegistry)
        {
            g_MindControlSelectCommandRegistry = new MindControlSelectCommandRegistry(callback);
        }
        return g_MindControlSelectCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableMindControlSelectCommand()
    {
        if (g_MindControlSelectCommandRegistry)
        {
            g_MindControlSelectCommandRegistry->Disable();
        }
    }
}
