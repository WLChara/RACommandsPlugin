#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace ra_commands::game::selection_capacity
{
    struct State
    {
        bool mFixedAvailable;
        bool mDynamicAvailable;
        std::size_t mCapacity;
    };

    // 在版本门禁通过且会话就绪的游戏线程安装。失败不会阻塞其他命令。
    bool Install(std::string& outError);
    // 只在会话或代次变化时清空候选表；所有调用须在安装所属游戏线程。
    void OnGameFrame(bool sessionActive, std::uint32_t epoch);
    // 与 OnGameFrame 相同的游戏线程读取快照；不是跨线程查询接口。
    State GetState();
    // 允许任意线程调用。仅禁止扩容，Hook 和原生表保留到进程退出。
    void Disable() noexcept;
}
