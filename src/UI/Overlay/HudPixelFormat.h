#pragma once
#include "UI/Overlay/HudGeometry.h"
#include "UI/Overlay/HudTypes.h"
#include <array>
#include <algorithm>

namespace ra_commands::ui
{
    /** 只在表面格式变化时解析通道；逐像素路径不查找位移。 */
    class HudPixelFormat
    {
    public:
        bool Configure(std::uint16_t red, std::uint16_t green, std::uint16_t blue)
        {
            if (!IsSupportedPixelMasks(red, green, blue)) return false;
            if (red == mRed.mMask && green == mGreen.mMask && blue == mBlue.mMask) return true;
            mRed = Channel(red); mGreen = Channel(green); mBlue = Channel(blue);
            mColorMask = red | green | blue;
            for (unsigned int pixel = 0; pixel < mBlack.size(); ++pixel)
                mBlack[pixel] = Blend(static_cast<std::uint16_t>(pixel), 0, 64);
            return true;
        }
        std::uint16_t Black(std::uint16_t pixel) const noexcept { return mBlack[pixel]; }
        std::uint16_t Blend(std::uint16_t background, std::uint16_t foreground, unsigned int alpha) const noexcept
        {
            alpha = (std::min)(256u, alpha);
            return static_cast<std::uint16_t>((background & ~mColorMask) |
                Mix(background, foreground, alpha, mRed) | Mix(background, foreground, alpha, mGreen) |
                Mix(background, foreground, alpha, mBlue));
        }
        std::uint16_t Pack(HudColor color) const noexcept
        {
            return static_cast<std::uint16_t>(((color.mRed * mRed.mMaximum + 127) / 255 << mRed.mShift) |
                ((color.mGreen * mGreen.mMaximum + 127) / 255 << mGreen.mShift) |
                ((color.mBlue * mBlue.mMaximum + 127) / 255 << mBlue.mShift));
        }
    private:
        struct ChannelData { unsigned int mMask = 0; unsigned int mShift = 0; unsigned int mMaximum = 0; };
        static ChannelData Channel(unsigned int mask)
        {
            unsigned int shift = 0;
            while (!((mask >> shift) & 1u)) ++shift;
            return {mask, shift, mask >> shift};
        }
        static unsigned int Mix(unsigned int background, unsigned int foreground, unsigned int alpha,
            const ChannelData& channel) noexcept
        {
            return (((((background & channel.mMask) >> channel.mShift) * (256 - alpha) +
                ((foreground & channel.mMask) >> channel.mShift) * alpha + 128) >> 8) << channel.mShift);
        }
        ChannelData mRed, mGreen, mBlue;
        unsigned int mColorMask = 0;
        std::array<std::uint16_t, 65536> mBlack{};
    };
}
