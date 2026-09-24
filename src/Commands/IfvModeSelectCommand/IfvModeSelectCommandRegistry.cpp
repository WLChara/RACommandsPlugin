#include "Commands/IfvModeSelectCommand/IfvModeSelectCommandRegistry.h"

#include "Commands/IfvModeSelectCommand/IfvKeyPressTracker.h"
#include "Commands/NativeCommandRegistry.h"

namespace ra_commands::game
{
    namespace
    {
        class IfvModeSelectCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit IfvModeSelectCommandRegistry(void(*callback)(bool))
                : NativeCommandRegistry({
                    "RAIfvModeSelect",
                    L"IFV同类选择",
                    L"插件",
                    L"单击时选择屏幕内与所选 IFV 模式相同的单位；双击时选择全场同类单位"
                }),
                mCallback(callback)
            {
            }

            // 游戏默认条件会过滤 key-up；此命令需要释放事件来屏蔽长按自动重复。
            bool vt_entry_18(DWORD) const override
            {
                return true;
            }

            void ResetInput() const noexcept
            {
                mKeyPressTracker.Reset();
            }

        protected:
            void OnExecute(DWORD argument) const override
            {
                const auto phase = mKeyPressTracker.Consume(argument);
                if (mCallback && phase != ifv_select::PressPhase::Ignored)
                {
                    mCallback(phase == ifv_select::PressPhase::SecondPress);
                }
            }

        private:
            void(*mCallback)(bool);
            mutable ifv_select::IfvKeyPressTracker mKeyPressTracker;
        };

        // 游戏命令表与热键表会持有 this，因此实例保持到进程退出。
        IfvModeSelectCommandRegistry* g_IfvModeSelectCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterIfvModeSelectCommand(
        const GameSymbols& symbols,
        void(*callback)(bool),
        std::string& outError)
    {
        if (!callback)
        {
            outError = "IFV mode selection callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_IfvModeSelectCommandRegistry)
        {
            g_IfvModeSelectCommandRegistry = new IfvModeSelectCommandRegistry(callback);
        }
        return g_IfvModeSelectCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableIfvModeSelectCommand()
    {
        if (g_IfvModeSelectCommandRegistry)
        {
            g_IfvModeSelectCommandRegistry->ResetInput();
            g_IfvModeSelectCommandRegistry->Disable();
        }
    }
}
