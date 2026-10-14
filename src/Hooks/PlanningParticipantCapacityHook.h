#pragma once
#include <string>

namespace ra_commands::hooks::planning_participant_capacity
{
    using BypassQuery = bool(*)() noexcept;
    /** 游戏层拥有线程和会话门禁；本层只拥有中段 Hook 和原生跳板。
     * 严格匹配数量 cmp；不改函数入口，既有入口包装仍在放行/回退时执行。
     * 安装成功后常驻至进程退出，重复调用只能来自原安装游戏线程。
     */
    bool Install(BypassQuery query, std::string& outError);
}
