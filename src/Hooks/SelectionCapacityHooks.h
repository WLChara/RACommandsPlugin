#pragma once
#include <string>

namespace ra_commands::hooks::selection_capacity
{
    // 游戏层拥有全部候选状态；这些端口只允许安装所属线程访问。
    struct Callbacks
    {
        void (*mOnEpoch)(void*);
        bool (*mEnsureBuildRoom)(void*, int, int);
        bool (*mBeginWriter)();
        void (*mEndWriter)();
        bool (*mEnsureAddRoom)(void*, int, int);
        bool (*mBeginReader)(void*);
        void (*mEndReader)();
    };

    // 只校验中段指令边界；入口 Hook 由 MinHook 沿当前入口构造跳板。
    bool VerifyGates();
    bool Install(const Callbacks& callbacks, std::string& outError);
}
