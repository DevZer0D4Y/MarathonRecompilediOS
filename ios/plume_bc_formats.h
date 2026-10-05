#pragma once
#include "bc_upload.h"

namespace plume
{
    // Set when the device samples BC textures natively (MTLDevice.supportsBCTextureCompression).
    // Only GPUs without BC support need the CPU decoder below, which uses 4-8x the texture memory.
    inline bool g_iosNativeBC = false;

    inline bool IOSIsSRGBFormat(RenderFormat format)
    {
        if (g_iosNativeBC)
            return false;

        return format == RenderFormat::BC1_UNORM_SRGB || format == RenderFormat::BC2_UNORM_SRGB
            || format == RenderFormat::BC3_UNORM_SRGB || format == RenderFormat::BC7_UNORM_SRGB;
    }
    inline RenderFormat IOSFallbackFormat(RenderFormat format)
    {
        if (g_iosNativeBC)
            return format;

        switch (format)
        {
        case RenderFormat::BC1_TYPELESS: case RenderFormat::BC1_UNORM:
        case RenderFormat::BC2_TYPELESS: case RenderFormat::BC2_UNORM:
        case RenderFormat::BC3_TYPELESS: case RenderFormat::BC3_UNORM:
        case RenderFormat::BC7_TYPELESS: case RenderFormat::BC7_UNORM:
            return RenderFormat::R8G8B8A8_UNORM;
        case RenderFormat::BC1_UNORM_SRGB: case RenderFormat::BC2_UNORM_SRGB:
        case RenderFormat::BC3_UNORM_SRGB: case RenderFormat::BC7_UNORM_SRGB:
            return RenderFormat::R8G8B8A8_UNORM;
        case RenderFormat::BC4_TYPELESS: case RenderFormat::BC4_UNORM: return RenderFormat::R8_UNORM;
        case RenderFormat::BC4_SNORM: return RenderFormat::R8_SNORM;
        case RenderFormat::BC5_TYPELESS: case RenderFormat::BC5_UNORM: return RenderFormat::R8G8_UNORM;
        case RenderFormat::BC5_SNORM: return RenderFormat::R8G8_SNORM;
        case RenderFormat::BC6H_TYPELESS: case RenderFormat::BC6H_UF16: case RenderFormat::BC6H_SF16:
            return RenderFormat::R16G16B16A16_FLOAT;
        default: return format;
        }
    }
    inline marathon::ios::BCFormat IOSBCFormat(RenderFormat format)
    {
        using marathon::ios::BCFormat;
        switch (format)
        {
        case RenderFormat::BC1_TYPELESS: case RenderFormat::BC1_UNORM: case RenderFormat::BC1_UNORM_SRGB: return BCFormat::BC1;
        case RenderFormat::BC2_TYPELESS: case RenderFormat::BC2_UNORM: case RenderFormat::BC2_UNORM_SRGB: return BCFormat::BC2;
        case RenderFormat::BC3_TYPELESS: case RenderFormat::BC3_UNORM: case RenderFormat::BC3_UNORM_SRGB: return BCFormat::BC3;
        case RenderFormat::BC4_SNORM: return BCFormat::BC4Signed;
        case RenderFormat::BC4_TYPELESS: case RenderFormat::BC4_UNORM: return BCFormat::BC4;
        case RenderFormat::BC5_SNORM: return BCFormat::BC5Signed;
        case RenderFormat::BC5_TYPELESS: case RenderFormat::BC5_UNORM: return BCFormat::BC5;
        case RenderFormat::BC6H_SF16: return BCFormat::BC6Signed;
        case RenderFormat::BC6H_TYPELESS: case RenderFormat::BC6H_UF16: return BCFormat::BC6;
        default: return BCFormat::BC7;
        }
    }
}
