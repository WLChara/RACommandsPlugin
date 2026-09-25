#include "Commands/AutoCrushAddCommand/AutoCrushAddCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoCrushAddCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AutoCrushAddCommandRegistry(AutoCrushAddExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAutoCrushAddSelected",
                    L"选中单位自动碾压",
                    L"插件",
                    L"将选中的可碾压载具加入持续自动碾压部队"
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
            AutoCrushAddExecuteCallback mCallback;
        };

        // 原生命令表在整个进程存续期持有对象。
        AutoCrushAddCommandRegistry* g_Registry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoCrushAddCommand(
        const GameSymbols& symbols,
        AutoCrushAddExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto-crush add command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_Registry)
        {
            g_Registry = new AutoCrushAddCommandRegistry(callback);
        }
        return g_Registry->TryRegister(symbols, outError);
    }

    void DisableAutoCrushAddCommand()
    {
        if (g_Registry)
        {
            g_Registry->Disable();
        }
    }
}
