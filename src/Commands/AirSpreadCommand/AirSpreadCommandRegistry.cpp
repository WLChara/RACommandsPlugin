#include "Commands/AirSpreadCommand/AirSpreadCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AirSpreadCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AirSpreadCommandRegistry(AirSpreadExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAirSpreadMove",
                    L"空军分散移动",
                    L"插件",
                    L"将选中的飞行单位分散移动到鼠标附近"
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
            AirSpreadExecuteCallback mCallback;
        };

        // 原生命令表在整个进程存续期持有对象。
        AirSpreadCommandRegistry* g_AirSpreadCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAirSpreadCommand(
        const GameSymbols& symbols,
        AirSpreadExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "air-spread command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AirSpreadCommandRegistry)
        {
            g_AirSpreadCommandRegistry = new AirSpreadCommandRegistry(callback);
        }
        return g_AirSpreadCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAirSpreadCommand()
    {
        if (g_AirSpreadCommandRegistry)
        {
            g_AirSpreadCommandRegistry->Disable();
        }
    }
}
