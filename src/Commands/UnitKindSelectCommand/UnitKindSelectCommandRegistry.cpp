#include "Commands/UnitKindSelectCommand/UnitKindSelectCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class UnitKindSelectCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit UnitKindSelectCommandRegistry(void(*callback)())
                : NativeCommandRegistry({
                    "RAUnitKindSelect",
                    L"按兵种筛选",
                    L"插件",
                    L"每次按键轮换筛选载具、步兵与飞机"
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
        UnitKindSelectCommandRegistry* g_UnitKindSelectCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterUnitKindSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "unit-kind selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_UnitKindSelectCommandRegistry)
        {
            g_UnitKindSelectCommandRegistry = new UnitKindSelectCommandRegistry(callback);
        }
        return g_UnitKindSelectCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableUnitKindSelectCommand()
    {
        if (g_UnitKindSelectCommandRegistry)
        {
            g_UnitKindSelectCommandRegistry->Disable();
        }
    }
}
