#pragma once
#include <algorithm>

inline float FadeProgress(float elapsed, float duration)
{
    return duration > 0.0f ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
}
