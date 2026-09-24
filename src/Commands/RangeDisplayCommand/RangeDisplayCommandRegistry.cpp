#include "Commands/RangeDisplayCommand/RangeDisplayCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class RangeDisplayCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit RangeDisplayCommandRegistry(RangeDisplayExecuteCallback callback)
                : NativeCommandRegistry({
                    "RARangeDisplay",
                    L"显示选中单位射程",
                    L"插件",
                    L"开启或关闭选中单位当前武器的静态射程圈"
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
            RangeDisplayExecuteCallback mCallback;
        };

        // 游戏原生命令表会长期持有该对象。
        RangeDisplayCommandRegistry* g_RangeDisplayCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterRangeDisplayCommand(
        const GameSymbols& symbols,
        RangeDisplayExecuteCallback callback,
        std::string& outError)
    {
        if (!callback)
        {
            outError = "range-display command callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_RangeDisplayCommandRegistry)
        {
            g_RangeDisplayCommandRegistry = new RangeDisplayCommandRegistry(callback);
        }
        return g_RangeDisplayCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableRangeDisplayCommand()
    {
        if (g_RangeDisplayCommandRegistry)
        {
            g_RangeDisplayCommandRegistry->Disable();
        }
    }
}
