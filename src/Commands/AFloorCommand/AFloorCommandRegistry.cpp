#include "Commands/AFloorCommand/AFloorCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AFloorCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AFloorCommandRegistry(AFloorExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAFloorMode",
                    L"A地板模式",
                    L"插件",
                    L"开启或关闭仅作用于本地输入的A地板模式"
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
            AFloorExecuteCallback mCallback;
        };

        // 原生命令表在整个进程存续期持有对象。
        AFloorCommandRegistry* g_AFloorCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterAFloorCommand(
        const GameSymbols& symbols,
        AFloorExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "A-floor command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_AFloorCommandRegistry)
        {
            g_AFloorCommandRegistry = new AFloorCommandRegistry(callback);
        }
        return g_AFloorCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableAFloorCommand()
    {
        if (g_AFloorCommandRegistry)
        {
            g_AFloorCommandRegistry->Disable();
        }
    }
}
