#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
#define BCDEC_STATIC
#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION
#include "bcdec.h"

namespace marathon::ios
{
    enum class BCFormat { BC1, BC2, BC3, BC4, BC4Signed, BC5, BC5Signed, BC6, BC6Signed, BC7 };
    struct DecodedImage
    {
        std::vector<uint8_t> bytes;
        uint32_t rowPitch = 0;
        uint32_t imagePitch = 0;
    };

    struct BCImageLayout
    {
        uint32_t rowPitch = 0;
        uint32_t imagePitch = 0;
        size_t byteSize = 0;
    };

    inline bool GetBCImageLayout(BCFormat format, uint32_t width, uint32_t height,
        uint32_t depth, BCImageLayout& layout)
    {
        if (!width || !height || !depth) return false;
        const bool r = format == BCFormat::BC4 || format == BCFormat::BC4Signed;
        const bool rg = format == BCFormat::BC5 || format == BCFormat::BC5Signed;
        const bool hdr = format == BCFormat::BC6 || format == BCFormat::BC6Signed;
        const uint32_t pixelSize = r ? 1 : rg ? 2 : hdr ? 8 : 4;
        const uint64_t rowPitch = (uint64_t(width) * pixelSize + 255) & ~uint64_t(255);
        if (rowPitch > UINT32_MAX) return false;
        const uint64_t imagePitch = rowPitch * height;
        if (imagePitch > UINT32_MAX || imagePitch > SIZE_MAX / depth)
            return false;
        layout = {uint32_t(rowPitch), uint32_t(imagePitch), size_t(imagePitch * depth)};
        return true;
    }

    // Preserve every mip and array/volume slice. Cropping matters for small mips
    // whose dimensions are less than the BC format's 4x4 block size.
    inline bool DecodeBCImageInto(BCFormat format, uint32_t width, uint32_t height, uint32_t depth,
        const uint8_t* source, size_t sourceSize, uint32_t sourceRowPitch,
        uint32_t sourceImagePitch, uint8_t* destination, size_t destinationSize)
    {
        if (!source || !destination) return false;
        BCImageLayout layout;
        if (!GetBCImageLayout(format, width, height, depth, layout) || destinationSize < layout.byteSize)
            return false;
        const bool r = format == BCFormat::BC4 || format == BCFormat::BC4Signed;
        const bool rg = format == BCFormat::BC5 || format == BCFormat::BC5Signed;
        const bool hdr = format == BCFormat::BC6 || format == BCFormat::BC6Signed;
        const uint32_t pixelSize = r ? 1 : rg ? 2 : hdr ? 8 : 4;
        const uint32_t blockSize = format == BCFormat::BC1 || r ? 8 : 16;
        uint64_t blocksX = (uint64_t(width) + 3) / 4, blocksY = (uint64_t(height) + 3) / 4;
        if (blocksX * blockSize > sourceRowPitch || uint64_t(sourceRowPitch) * blocksY > sourceImagePitch)
            return false;
        uint64_t required = uint64_t(sourceImagePitch) * (depth - 1)
            + uint64_t(sourceRowPitch) * (blocksY - 1) + blocksX * blockSize;
        if (required > sourceSize) return false;
        // Only alignment padding needs clearing; every visible texel is overwritten below.
        const size_t visibleRowBytes = size_t(width) * pixelSize;
        if (visibleRowBytes < layout.rowPitch)
            for (uint32_t z = 0; z < depth; ++z)
            for (uint32_t y = 0; y < height; ++y)
                std::memset(destination + size_t(z) * layout.imagePitch + size_t(y) * layout.rowPitch
                    + visibleRowBytes, 0, layout.rowPitch - visibleRowBytes);

        for (uint32_t z = 0; z < depth; ++z)
        for (uint32_t by = 0; by < blocksY; ++by)
        for (uint32_t bx = 0; bx < blocksX; ++bx)
        {
            alignas(8) uint8_t compressed[16]{};
            alignas(8) uint8_t pixels[128]{};
            std::memcpy(compressed, source + uint64_t(z) * sourceImagePitch
                + uint64_t(by) * sourceRowPitch + bx * blockSize, blockSize);
            switch (format)
            {
            case BCFormat::BC1: bcdec_bc1(compressed, pixels, 16); break;
            case BCFormat::BC2: bcdec_bc2(compressed, pixels, 16); break;
            case BCFormat::BC3: bcdec_bc3(compressed, pixels, 16); break;
            case BCFormat::BC4: bcdec_bc4(compressed, pixels, 4, 0); break;
            case BCFormat::BC4Signed: bcdec_bc4(compressed, pixels, 4, 1); break;
            case BCFormat::BC5: bcdec_bc5(compressed, pixels, 8, 0); break;
            case BCFormat::BC5Signed: bcdec_bc5(compressed, pixels, 8, 1); break;
            case BCFormat::BC7: bcdec_bc7(compressed, pixels, 16); break;
            case BCFormat::BC6:
            case BCFormat::BC6Signed:
            {
                uint16_t rgb[48]{};
                bcdec_bc6h_half(compressed, rgb, 12, format == BCFormat::BC6Signed);
                for (size_t i = 0; i < 16; ++i)
                {
                    std::memcpy(pixels + i * 8, rgb + i * 3, 6);
                    const uint16_t alpha = 0x3C00; // 1.0 in IEEE half precision.
                    std::memcpy(pixels + i * 8 + 6, &alpha, 2);
                }
                break;
            }
            }
            uint32_t rows = std::min(4u, height - by * 4);
            uint32_t columns = std::min(4u, width - bx * 4);
            for (uint32_t y = 0; y < rows; ++y)
                std::memcpy(destination + uint64_t(z) * layout.imagePitch
                    + uint64_t(by * 4 + y) * layout.rowPitch + bx * 4 * pixelSize,
                    pixels + y * 4 * pixelSize, columns * pixelSize);
        }
        return true;
    }

    inline bool DecodeBCImage(BCFormat format, uint32_t width, uint32_t height, uint32_t depth,
        const uint8_t* source, size_t sourceSize, uint32_t sourceRowPitch,
        uint32_t sourceImagePitch, DecodedImage& image)
    {
        BCImageLayout layout;
        if (!GetBCImageLayout(format, width, height, depth, layout)) return false;
        // Reject truncated sources before allocating the destination.
        const bool r = format == BCFormat::BC4 || format == BCFormat::BC4Signed;
        const uint32_t blockSize = format == BCFormat::BC1 || r ? 8 : 16;
        const uint64_t blocksX = (uint64_t(width) + 3) / 4, blocksY = (uint64_t(height) + 3) / 4;
        if (!source || blocksX * blockSize > sourceRowPitch
            || uint64_t(sourceRowPitch) * blocksY > sourceImagePitch
            || uint64_t(sourceImagePitch) * (depth - 1) + uint64_t(sourceRowPitch) * (blocksY - 1)
                + blocksX * blockSize > sourceSize) return false;
        image.bytes.resize(layout.byteSize);
        if (!DecodeBCImageInto(format, width, height, depth, source, sourceSize, sourceRowPitch,
            sourceImagePitch, image.bytes.data(), image.bytes.size())) return false;
        image.rowPitch = layout.rowPitch;
        image.imagePitch = layout.imagePitch;
        return true;
    }

}
