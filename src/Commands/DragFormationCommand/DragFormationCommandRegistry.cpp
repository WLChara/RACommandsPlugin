#include "Commands/DragFormationCommand/DragFormationCommandRegistry.h"

#include "Commands/NativeCommandRegistry.h"
#include "Commands/CommandCategories.h"

namespace ra_commands::game
{
    namespace
    {
        class DragFormationCommandRegistry final : public NativeCommandRegistry
        {
        public:
            explicit DragFormationCommandRegistry(void(*callback)())
                : NativeCommandRegistry({"RADragFormationDemo", L"拖拽矩形编队（演示）",
                    command_categories::FORMATION_AND_TRANSPORT, L"本方步兵和载具矩形编队，不含飞机类单位；同时按住左右键0.5秒拖拽，松开执行，Esc取消"}),
                  mCallback(callback) { }

        protected:
            void OnExecute(DWORD) const override
            {
                if (mCallback) { mCallback(); }
            }

        private:
            void(*mCallback)();
        };

        // 原生命令表持有对象；停用只撤掉执行回调，存储保持到进程退出。
        DragFormationCommandRegistry* g_DragFormationCommandRegistry = nullptr;
    }

    CommandRegistrationResult TryRegisterDragFormationCommand(
        const GameSymbols& symbols, void(*callback)(), std::string& outError)
    {
        if (!callback)
        {
            outError = "drag-formation callback is null";
            return CommandRegistrationResult::Failed;
        }
        if (!g_DragFormationCommandRegistry)
        {
            g_DragFormationCommandRegistry = new DragFormationCommandRegistry(callback);
        }
        return g_DragFormationCommandRegistry->TryRegister(symbols, outError);
    }

    void DisableDragFormationCommand()
    {
        if (g_DragFormationCommandRegistry) { g_DragFormationCommandRegistry->Disable(); }
    }
}
