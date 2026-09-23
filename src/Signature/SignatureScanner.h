#pragma once

#include "Signature/PeImage.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ra_commands::memory
{
    enum class ScanError
    {
        None,
        InvalidModule,
        SectionMissing,
        InvalidSignature,
        NoMatch,
        MultipleMatches
    };

    struct ScanResult
    {
        std::uintptr_t Address = 0;
        ScanError Error = ScanError::None;
    };

    /**
     * 在当前进程已加载模块的指定 PE 节中查找唯一 AOB。
     * 只读扫描；目标模块由调用方提供，失败时不返回任何地址。
     */
    ScanResult ScanUnique(
        const PeImage& image,
        std::string_view sectionName,
        std::string_view signature);

    const char* GetScanErrorText(ScanError error);
}
