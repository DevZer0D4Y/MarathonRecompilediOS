#pragma once
#include <cmath>
#include <cstddef>

template<class Sample>
void InterleavePlanarAudio(const Sample* planar, float* interleaved, size_t frames, size_t channels, float volume)
{
    for (size_t frame = 0; frame < frames; ++frame)
        for (size_t channel = 0; channel < channels; ++channel)
        {
            float value = float(planar[channel * frames + frame]) * volume;
            interleaved[frame * channels + channel] = std::isfinite(value) ? value : 0.0f;
        }
}
