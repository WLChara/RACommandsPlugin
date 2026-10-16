#pragma once

namespace ra_commands::game::command_categories
{
    // 原生热键界面持有分类字符串；统一使用进程存续期有效的静态存储。
    inline constexpr wchar_t UNIT_SELECTION[] = L"插件：单位选择";
    inline constexpr wchar_t FORMATION_AND_TRANSPORT[] = L"插件：编队与运输";
    inline constexpr wchar_t COMBAT_ASSISTANCE[] = L"插件：战斗辅助";
    inline constexpr wchar_t BUILDING_MANAGEMENT[] = L"插件：建筑管理";
    inline constexpr wchar_t SUPER_WEAPONS[] = L"插件：超级武器";
    inline constexpr wchar_t DISPLAY_AND_TOOLS[] = L"插件：显示与工具";
}
