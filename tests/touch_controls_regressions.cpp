#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <SDL.h>

// Use the exact Xbox input ABI without the generated PPC headers. The test
// compiles the production control implementation and replaces only host APIs.
struct XAMINPUT_GAMEPAD {
    uint16_t wButtons{};
    uint8_t bLeftTrigger{}, bRightTrigger{};
    int16_t sThumbLX{}, sThumbLY{}, sThumbRX{}, sThumbRY{};
};
constexpr uint16_t XAMINPUT_GAMEPAD_DPAD_UP=1, XAMINPUT_GAMEPAD_DPAD_DOWN=2;
constexpr uint16_t XAMINPUT_GAMEPAD_DPAD_LEFT=4, XAMINPUT_GAMEPAD_DPAD_RIGHT=8;
constexpr uint16_t XAMINPUT_GAMEPAD_START=16, XAMINPUT_GAMEPAD_BACK=32;
constexpr uint16_t XAMINPUT_GAMEPAD_LEFT_SHOULDER=256, XAMINPUT_GAMEPAD_RIGHT_SHOULDER=512;
constexpr uint16_t XAMINPUT_GAMEPAD_A=4096, XAMINPUT_GAMEPAD_B=8192;
constexpr uint16_t XAMINPUT_GAMEPAD_X=16384, XAMINPUT_GAMEPAD_Y=32768;
static double testTime=100;
static SDL_EventFilter testEventWatch=nullptr;
extern "C" Uint64 SDL_GetPerformanceCounter() { return Uint64(testTime*1000000); }
extern "C" Uint64 SDL_GetPerformanceFrequency() { return 1000000; }
static int testWindowWidth=1280, testWindowHeight=720;
extern "C" void SDL_GetWindowSize(SDL_Window*, int* w, int* h) { *w=testWindowWidth; *h=testWindowHeight; }
extern "C" void SDL_AddEventWatch(SDL_EventFilter callback, void*) { testEventWatch=callback; }
namespace TouchHaptics { void Init() {} void Play(bool) {} }
#define MARATHON_RECOMP_TOUCH_TEST
#include "../MarathonRecomp/os/ios/touch_controls.cpp"
#include "../MarathonRecomp/ui/fader.cpp"

static void Touch(Uint32 type, SDL_FingerID id, float x, float y, float dx=0, float dy=0)
{
    SDL_Event event{};
    event.type=type; event.tfinger.fingerId=id;
    event.tfinger.x=x/1280; event.tfinger.y=y/720;
    event.tfinger.dx=dx/1280; event.tfinger.dy=dy/720;
    testEventWatch(nullptr, &event);
}
static XAMINPUT_GAMEPAD Pad() { XAMINPUT_GAMEPAD pad{}; TouchControls::Apply(pad); return pad; }

static void CheckFades()
{
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.DisplaySize={1280, 720};
    io.IniFilename=nullptr;
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    bool completed=false;
    auto draw=[&](float delta) {
        io.DeltaTime=delta;
        ImGui::NewFrame();
        Fader::Draw();
        auto list=ImGui::GetBackgroundDrawList();
        int alpha=list->VtxBuffer.empty() ? 0 : int(list->VtxBuffer.back().col >> IM_COL32_A_SHIFT);
        ImGui::Render();
        return alpha;
    };

    Fader::FadeOut(2, [&] { completed=true; }, 0.75f);
    assert(draw(1)==127 && !completed);
    assert(draw(1)==255 && !completed); // Duration is two seconds, not its square.
    assert(draw(0.5f)==255 && !completed);
    assert(draw(0.25f)==255 && completed);
    completed=false;
    Fader::FadeIn(0, [&] { completed=true; }, 0.75f);
    assert(draw(0.1f)==0 && !completed); // Remain transparent during callback delay.
    assert(draw(0.65f)==0 && completed);
    Fader::FadeOut(0, [] { Fader::FadeIn(1, nullptr, 0); }, 0);
    assert(draw(0.1f)==255); // The callback may start another fade.
    assert(draw(1)==0);
    ImGui::DestroyContext();
    std::puts("Passed: production fade rendering, callback delay, zero duration, callback reentry.");
}

int main(int argc, char** argv)
{
    assert(argc==2);
    touchTestDirectory=argv[1];
    TouchControls::Init();
    assert(testEventWatch && TouchControls::IsActive());
    assert(Pad().wButtons==0);

    Touch(SDL_FINGERDOWN, 1, 1014.4f, 556.2f); // A at its original position.
    assert(Pad().wButtons & XAMINPUT_GAMEPAD_A);
    Touch(SDL_FINGERUP, 1, 1014.4f, 556.2f);
    testTime+=0.05;
    assert(Pad().wButtons & XAMINPUT_GAMEPAD_A); // Keep short taps visible at low FPS.
    testTime+=0.06;
    assert(Pad().wButtons==0);

    // Fixed stick: the stick and its D-pad ring stay at their layout position.
    Config::TouchDynamicStick=false;
    Touch(SDL_FINGERDOWN, 2, 192, 516.6f);
    Touch(SDL_FINGERMOTION, 2, 194, 516.6f, 2, 0);
    assert(Pad().sThumbLX==0); // Stick deadzone.
    Touch(SDL_FINGERMOTION, 2, 310, 516.6f, 116, 0);
    Touch(SDL_FINGERDOWN, 3, 1014.4f, 556.2f);
    assert(Pad().sThumbLX==32767 && (Pad().wButtons & XAMINPUT_GAMEPAD_A));
    TouchControls::Reset();
    assert(Pad().sThumbLX==0 && Pad().wButtons==0);

    Touch(SDL_FINGERDOWN, 4, 192, 420);
    assert(Pad().wButtons & XAMINPUT_GAMEPAD_DPAD_UP);
    TouchControls::Reset();

    // After the app switcher SDL can report the portrait size while the game is drawn in landscape.
    g_swapChainLandscape=1;
    testWindowWidth=720; testWindowHeight=1280;
    testTime+=0.2;
    Touch(SDL_FINGERDOWN, 17, 1014.4f, 556.2f);
    assert(Pad().wButtons & XAMINPUT_GAMEPAD_A);
    assert(g_windowSize.x==1280 && g_windowSize.y==720);
    TouchControls::Reset();
    testWindowWidth=1280; testWindowHeight=720;
    testTime+=0.2;

    // Floating stick: it appears under the thumb anywhere in the lower left and steers from there.
    Config::TouchDynamicStick=true;
    Touch(SDL_FINGERDOWN, 13, 400, 600);
    assert(Pad().sThumbLX==0 && Pad().wButtons==0);
    Touch(SDL_FINGERMOTION, 13, 400, 480, 0, -120);
    assert(Pad().sThumbLY==32767 && Pad().sThumbRY==0);
    Touch(SDL_FINGERUP, 13, 400, 480);
    assert(Pad().sThumbLY==0);
    Touch(SDL_FINGERDOWN, 14, 192, 420); // The old D-pad ring area also starts the stick.
    assert(Pad().wButtons==0);
    TouchControls::Reset();
    Touch(SDL_FINGERDOWN, 15, 250, 120); // The top of the screen still looks around.
    Touch(SDL_FINGERMOTION, 15, 258, 120, 8, 0);
    assert(Pad().sThumbRX>30000 && Pad().sThumbLX==0);
    TouchControls::Reset();
    testTime+=0.2;
    Touch(SDL_FINGERDOWN, 16, 1014.4f, 556.2f); // Face buttons keep priority over the stick zone.
    assert(Pad().wButtons & XAMINPUT_GAMEPAD_A);
    TouchControls::Reset();
    testTime+=0.2;

    Touch(SDL_FINGERDOWN, 5, 650, 300); // Free area is swipe-to-look.
    Touch(SDL_FINGERMOTION, 5, 658, 296, 8, -4);
    assert(Pad().sThumbRX>30000 && Pad().sThumbRY>30000);
    testTime+=0.11;
    assert(Pad().sThumbRX==0 && Pad().sThumbRY==0);
    TouchControls::Reset();

    Touch(SDL_FINGERDOWN, 6, 1014.4f, 556.2f);
    TouchControls::OnPhysicalControllerInput();
    assert(!TouchControls::IsActive() && Pad().wButtons==0);
    Touch(SDL_FINGERDOWN, 7, 500, 300);
    assert(TouchControls::IsActive() && Pad().wButtons==0);
    TouchControls::Reset();

    Touch(SDL_FINGERDOWN, 8, 419.2f, 72.72f); // EDIT.
    Touch(SDL_FINGERUP, 8, 419.2f, 72.72f);
    Touch(SDL_FINGERDOWN, 9, 1014.4f, 556.2f);
    Touch(SDL_FINGERMOTION, 9, 1078.4f, 577.8f, 64, 21.6f);
    assert(Pad().wButtons==0); // Editing must not send game input.
    Touch(SDL_FINGERUP, 9, 1078.4f, 577.8f);
    Touch(SDL_FINGERDOWN, 10, 1139.2f, 658.8f); // DONE: persist layout.
    assert(Config::saveCount==1);
    assert(std::filesystem::exists(touchTestDirectory/"touch_layout.toml"));
    TouchControls::Reset();
    testTime+=0.2;
    Touch(SDL_FINGERDOWN, 11, 1078.4f, 577.8f);
    assert(Pad().wButtons & XAMINPUT_GAMEPAD_A);
    TouchControls::Reset();

    InstallerWizard::s_isVisible=true;
    Touch(SDL_FINGERDOWN, 12, 1078.4f, 577.8f);
    assert(!TouchControls::IsActive() && Pad().wButtons==0);
    InstallerWizard::s_isVisible=false;
    Config::TouchControls=false;
    assert(!TouchControls::IsActive());
    Config::TouchControls=true;
    std::ofstream(touchTestDirectory/"touch_layout.toml") << "[a]\nx = nan\ny = 0.9\nwidth = 0.1\nheight = 0.2\n";
    TouchControls::Init();
    assert(std::isfinite(g_layout[10].frame.x));
    assert(g_layout[10].frame.x+g_layout[10].frame.width<=1);
    assert(g_layout[10].frame.y+g_layout[10].frame.height<=1);
    std::puts("Passed: imported Unleashed controls, multi-touch, tap timing, deadzone, D-pad, swipe camera, controller switching, reset, layout editing/persistence, floating stick, stale portrait window size.");
    CheckFades();
}
