#include "fader.h"
#include <utils/fade_timing.h>

static bool g_isFading;
static bool g_isFadeIn;

static double g_startTime;

static float g_duration;
static ImU32 g_colour = IM_COL32_BLACK;
static std::function<void()> g_endCallback;
static float g_endCallbackDelay;

void Fader::Draw()
{
    if (!s_isVisible)
        return;

    const double now = ImGui::GetTime();
    const float elapsed = float(now - g_startTime);
    if (g_isFading && elapsed >= g_duration + g_endCallbackDelay)
    {
        g_isFading = false;
        auto callback = std::move(g_endCallback);
        if (callback) callback();
    }

    if (g_isFadeIn && !g_isFading)
        return;

    // Callback delays must not undo the completed fade. A callback may also
    // start a new fade, so compute its opacity from the current state.
    const float time = FadeProgress(float(now - g_startTime), g_duration);
    const float alpha = g_isFadeIn ? 1.0f - time : time;
    auto colour = IM_COL32(g_colour & 0xFF, (g_colour >> 8) & 0xFF, (g_colour >> 16) & 0xFF, 255 * alpha);

    ImGui::GetBackgroundDrawList()->AddRectFilled({ 0, 0 }, ImGui::GetIO().DisplaySize, colour);
}

static void DoFade(bool isFadeIn, float duration, std::function<void()> endCallback, float endCallbackDelay)
{
    if (g_isFading)
        return;

    g_isFading = true;
    g_isFadeIn = isFadeIn;
    g_startTime = ImGui::GetTime();
    g_duration = std::max(duration, 0.0f);
    g_endCallback = endCallback;
    g_endCallbackDelay = endCallbackDelay;

    Fader::s_isVisible = true;
}

void Fader::SetFadeColour(ImU32 colour)
{
    g_colour = colour;
}

void Fader::FadeIn(float duration, std::function<void()> endCallback, float endCallbackDelay)
{
    DoFade(true, duration, endCallback, endCallbackDelay);
}

void Fader::FadeOut(float duration, std::function<void()> endCallback, float endCallbackDelay)
{
    DoFade(false, duration, endCallback, endCallbackDelay);
}
