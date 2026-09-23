#pragma once

#include <cstdint>
#include <string>

namespace ra_commands::game
{
    /**
     * 当前受支持的 gamemd.exe 命令系统入口。
     * 地址由运行时 AOB 解析；使用已核对的 ABI 调用游戏热键加载函数。
     * 所有入口仅适用于经 TargetVersion 验证的目标样本。
     */
    class GameSymbols
    {
    public:
        bool Resolve(std::uintptr_t moduleBase, std::string& outError);

        [[nodiscard]] bool IsReady() const noexcept;
        [[nodiscard]] bool MatchesCommandArrayAddress(std::uintptr_t address) const noexcept;
        bool ReloadKeyboardHotkeys() const;

    private:
        std::uintptr_t mCommandInitialization = 0;
        std::uintptr_t mKeyboardHotkeyLoader = 0;
        std::uintptr_t mCommandArray = 0;
        std::uintptr_t mHotkeyTable = 0;
    };
}
