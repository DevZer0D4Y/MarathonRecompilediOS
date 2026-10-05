#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <future>
#include <limits>
#include <thread>
#include <utils/audio_mix.h>
#include <utils/fade_timing.h>
#include <utils/thread_completion.h>
#include <utils/release_version.h>
#include <utils/virtual_memory.h>
#include <plume_render_interface_types.h>
#include "../ios/plume_bc_formats.h"
#ifndef _WIN32
#include <unistd.h>
#endif

int main()
{
    // A zero timeout must not report a running guest thread as complete; finite
    // waits must return, and all waiters must observe completion after Finish().
    ThreadCompletion completion;
    assert(!completion.Wait(0));
    auto start = std::chrono::steady_clock::now();
    assert(!completion.Wait(15));
    assert(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    auto waiter = std::async(std::launch::async, [&] { return completion.Wait(UINT32_MAX); });
    completion.Finish();
    assert(waiter.get());
    assert(completion.Wait(0));
    assert(completion.Wait(100));

    // All six channels must occupy unique, initialized interleaved positions.
    std::array<float, 18> planar{};
    for (size_t channel = 0; channel < 6; ++channel)
        for (size_t frame = 0; frame < 3; ++frame)
            planar[channel * 3 + frame] = float(channel * 10 + frame);
    std::array<float, 18> mixed;
    mixed.fill(-1000);
    InterleavePlanarAudio(planar.data(), mixed.data(), 3, 6, 0.5f);
    for (size_t frame = 0; frame < 3; ++frame)
        for (size_t channel = 0; channel < 6; ++channel)
            assert(mixed[frame * 6 + channel] == float(channel * 10 + frame) * 0.5f);
    planar[0] = std::numeric_limits<float>::quiet_NaN();
    planar[1] = std::numeric_limits<float>::infinity();
    InterleavePlanarAudio(planar.data(), mixed.data(), 3, 6, 1);
    assert(mixed[0] == 0 && mixed[6] == 0);

    assert(FadeProgress(0, 0)==1 && FadeProgress(1, 0)==1);
    assert(FadeProgress(0.5f, 2)==0.25f);
    assert(FadeProgress(2, 2)==1 && FadeProgress(5, 2)==1);
    assert(FadeProgress(-1, 2)==0);

    int major = 0, minor = 0, revision = 0;
    assert(ParseReleaseVersion("v1.2.3", major, minor, revision));
    assert(major == 1 && minor == 2 && revision == 3);
    for (const char* invalid : {"", "v", "1", "1.2", ".2.3", "1.2.3garbage", "1.2.-3", "999999999999.2.3"})
        assert(!ParseReleaseVersion(invalid, major, minor, revision));

#ifndef _WIN32
    assert(AllocateGuestMemory(SIZE_MAX) == nullptr);
    const long hostPageSize = sysconf(_SC_PAGESIZE);
    assert(hostPageSize > 0);
    const size_t pageSize = static_cast<size_t>(hostPageSize);
    uint8_t* memory = AllocateGuestMemory(pageSize * 2);
    assert(memory);
    memory[pageSize] = 42;
    assert(memory[pageSize] == 42);
    munmap(memory, pageSize * 2);
#endif

    assert(plume::IOSFallbackFormat(plume::RenderFormat::BC1_UNORM)==plume::RenderFormat::R8G8B8A8_UNORM);
    assert(plume::IOSFallbackFormat(plume::RenderFormat::BC7_UNORM_SRGB)==plume::RenderFormat::R8G8B8A8_UNORM);
    assert(plume::IOSFallbackFormat(plume::RenderFormat::BC5_SNORM)==plume::RenderFormat::R8G8_SNORM);
    assert(plume::IOSFallbackFormat(plume::RenderFormat::BC6H_UF16)==plume::RenderFormat::R16G16B16A16_FLOAT);
    assert(plume::IOSIsSRGBFormat(plume::RenderFormat::BC7_UNORM_SRGB));
    assert(!plume::IOSIsSRGBFormat(plume::RenderFormat::BC7_UNORM));
    using namespace marathon::ios;
    // A red BC1 block, cropped at the edges of a 1x1 mip.
    std::array<uint8_t, 8> red{0x00, 0xF8, 0, 0, 0, 0, 0, 0};
    DecodedImage image;
    assert(DecodeBCImage(BCFormat::BC1, 1, 1, 1, red.data(), red.size(), 8, 8, image));
    assert(image.bytes[0] == 255 && image.bytes[1] == 0 && image.bytes[2] == 0 && image.bytes[3] == 255);
    assert(image.rowPitch == 256 && image.imagePitch == 256);
    // The direct Metal upload path must overwrite visible pixels and clear padding,
    // while rejecting a too-small destination without touching it.
    BCImageLayout layout;
    assert(GetBCImageLayout(BCFormat::BC1, 1, 1, 1, layout));
    assert(layout.byteSize == 256);
    std::array<uint8_t, 257> upload;
    upload.fill(0xA5);
    assert(!DecodeBCImageInto(BCFormat::BC1, 1, 1, 1, red.data(), red.size(), 8, 8,
        upload.data() + 1, 255));
    assert(std::all_of(upload.begin(), upload.end(), [](uint8_t b) { return b == 0xA5; }));
    assert(DecodeBCImageInto(BCFormat::BC1, 1, 1, 1, red.data(), red.size(), 8, 8,
        upload.data() + 1, 256));
    assert(upload[0] == 0xA5);
    assert(std::equal(image.bytes.begin(), image.bytes.end(), upload.begin() + 1));
    assert(!GetBCImageLayout(BCFormat::BC6, UINT32_MAX, UINT32_MAX, UINT32_MAX, layout));
    assert(!DecodeBCImage(BCFormat::BC1, 4, 4, 1, red.data(), 7, 8, 8, image));
    assert(!DecodeBCImage(BCFormat::BC1, 4, 4, 1, red.data(), 8, 7, 8, image));
    // Row padding, non-multiple-of-four dimensions, and two volume slices.
    std::array<uint8_t, 64> blocks{};
    for (size_t i = 0; i < blocks.size(); i += 8) std::copy(red.begin(), red.end(), blocks.begin() + i);
    assert(DecodeBCImage(BCFormat::BC1, 5, 5, 2, blocks.data(), blocks.size(), 16, 32, image));
    for (uint32_t z = 0; z < 2; ++z)
        for (uint32_t y = 0; y < 5; ++y)
            for (uint32_t x = 0; x < 5; ++x)
                assert(image.bytes[z * image.imagePitch + y * image.rowPitch + x * 4] == 255);
    std::array<uint8_t, 16> redAlpha{};
    redAlpha[0] = 255;
    std::copy(red.begin(), red.end(), redAlpha.begin() + 8);
    assert(DecodeBCImage(BCFormat::BC3, 4, 4, 1, redAlpha.data(), 16, 16, 16, image));
    assert(image.bytes[0] == 255 && image.bytes[3] == 255);
    for (auto format : {BCFormat::BC2, BCFormat::BC4, BCFormat::BC4Signed, BCFormat::BC5, BCFormat::BC5Signed, BCFormat::BC6, BCFormat::BC6Signed, BCFormat::BC7})
        assert(DecodeBCImage(format, 4, 4, 1, redAlpha.data(), 16, 16, 16, image));
    std::puts("Passed: thread waits, surround audio, version parsing, allocation failure, fade timing, BC texture uploads.");
}
