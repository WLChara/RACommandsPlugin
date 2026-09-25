#include "Commands/AutoCrushRemoveCommand/AutoCrushRemoveCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class AutoCrushRemoveCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit AutoCrushRemoveCommandRegistry(AutoCrushRemoveExecuteCallback callback)
                : NativeCommandRegistry({
                    "RAAutoCrushRemoveSelected",
                    L"退出自动碾压",
                    L"插件",
                    L"取消选中载具的自动碾压标记和待发任务，不中断当前移动"
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
            AutoCrushRemoveExecuteCallback mCallback;
        };

        AutoCrushRemoveCommandRegistry* g_Registry = nullptr;
    }

    CommandRegistrationResult TryRegisterAutoCrushRemoveCommand(
        const GameSymbols& symbols,
        AutoCrushRemoveExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "auto-crush remove command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_Registry)
        {
            g_Registry = new AutoCrushRemoveCommandRegistry(callback);
        }
        return g_Registry->TryRegister(symbols, outError);
    }

    void DisableAutoCrushRemoveCommand()
    {
        if (g_Registry)
        {
            g_Registry->Disable();
        }
    }
}
