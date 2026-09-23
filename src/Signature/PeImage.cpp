#include "Signature/PeImage.h"

#include <Windows.h>

#include <algorithm>
#include <cstring>

namespace ra_commands::memory
{
    PeImage::PeImage(std::uintptr_t moduleBase)
    {
        if (moduleBase == 0)
        {
            return;
        }

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        {
            return;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(moduleBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt->OptionalHeader.SizeOfImage == 0)
        {
            return;
        }

        mModuleBase = moduleBase;
        mImageSize = nt->OptionalHeader.SizeOfImage;
        mSectionHeaders = reinterpret_cast<std::uintptr_t>(IMAGE_FIRST_SECTION(nt));
        mSectionCount = nt->FileHeader.NumberOfSections;
    }

    bool PeImage::IsValid() const noexcept
    {
        return mModuleBase != 0;
    }

    bool PeImage::Contains(std::uintptr_t address) const noexcept
    {
        return IsValid() && address >= mModuleBase &&
            address - mModuleBase < mImageSize;
    }

    SectionView PeImage::FindSection(std::string_view sectionName) const
    {
        if (!IsValid() || sectionName.empty() || sectionName.size() > IMAGE_SIZEOF_SHORT_NAME)
        {
            return {};
        }

        const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(mSectionHeaders);
        for (std::uint16_t index = 0; index < mSectionCount; ++index)
        {
            const auto& section = sections[index];
            char name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
            std::memcpy(name, section.Name, IMAGE_SIZEOF_SHORT_NAME);
            if (sectionName != name || section.VirtualAddress >= mImageSize)
            {
                continue;
            }

            const auto declaredSize = section.Misc.VirtualSize != 0
                ? section.Misc.VirtualSize
                : section.SizeOfRawData;
            const auto sectionSize = (std::min)(declaredSize, mImageSize - section.VirtualAddress);
            if (sectionSize == 0)
            {
                return {};
            }

            const auto* begin = reinterpret_cast<const std::byte*>(
                mModuleBase + section.VirtualAddress);
            return { begin, begin + sectionSize };
        }

        return {};
    }
}
