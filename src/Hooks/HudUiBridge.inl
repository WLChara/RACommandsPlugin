// 包含方提供 RestoreHud、RenderHud 和 g_MessageDraw；此文件同时用于本机 ABI 夹具。
// 两处桥接只在完整匹配目标指令后安装。保存全部 GPR/flags 与 x87/SSE，
// 因为恢复点处于函数中间，不能按普通函数的 caller-saved 规则丢弃现场。
__declspec(naked) void RestoreBridge()
{
    __asm
    {
        pushfd
        pushad
        mov ebp, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        call RestoreHud
        fxrstor [esp]
        mov esp, ebp
        popad
        popfd
        mov eax, [esi + 0Ch]
        mov dword ptr [esi + 0Ch], 0
        ret
    }
}
__declspec(naked) void DrawBridge()
{
    __asm
    {
        // 原生函数先执行，ECX/EDI 原样进入；其返回现场也原样保留。
        call dword ptr [g_MessageDraw]
        pushfd
        pushad
        mov ebp, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        call RenderHud
        fxrstor [esp]
        mov esp, ebp
        popad
        popfd
        ret
    }
}
