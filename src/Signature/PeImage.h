#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ra_commands::memory
{
    struct SectionView
    {
        const std::byte* Begin = nullptr;
        const std::byte* End = nullptr;
    };

    // 仅描述当前进程中已加载的 PE 映像，不拥有映像内存。
    class PeImage
    {
    public:
        explicit PeImage(std::uintptr_t moduleBase);

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool Contains(std::uintptr_t address) const noexcept;
        [[nodiscard]] SectionView FindSection(std::string_view sectionName) const;

    private:
        std::uintptr_t mModuleBase = 0;
        std::size_t mImageSize = 0;
        std::uintptr_t mSectionHeaders = 0;
        std::uint16_t mSectionCount = 0;
    };
}
