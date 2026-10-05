#include "platform_ios.h"
#include "touch_controls.h"
#include <os/process.h>
#include <os/media.h>
#include <os/user.h>
#include <os/version.h>
#include <ui/game_window.h>
#include <user/config.h>

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

std::filesystem::path ios::UserPath()
{
    @autoreleasepool
    {
        NSURL* documents = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory
            inDomains:NSUserDomainMask] firstObject];
        NSURL* directory = [documents URLByAppendingPathComponent:@"MarathonRecomp" isDirectory:YES];
        [[NSFileManager defaultManager] createDirectoryAtURL:directory
            withIntermediateDirectories:YES attributes:nil error:nil];
        return std::filesystem::path(directory.fileSystemRepresentation);
    }
}

void ios::Initialize()
{
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IDLE_TIMER_DISABLED, "1");
    // Hide the home indicator, and make the system's edge gestures (going home, Control Center) take a second
    // swipe, so that swiping the camera near the screen edges doesn't leave the game.
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:nil];
    [[AVAudioSession sharedInstance] setActive:YES error:nil];
}

void ios::HandleLifecycle(const SDL_Event& event)
{
    switch (event.type)
    {
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_TERMINATING:
        GameWindow::s_isFocused = false;
        TouchControls::Reset();
        Config::Save();
        break;
    case SDL_APP_DIDENTERFOREGROUND:
        GameWindow::s_isFocused = true;
        break;
    default:
        break;
    }
}

void ios::OpenURL(const char* url)
{
    NSString* text = [NSString stringWithUTF8String:url];
    dispatch_async(dispatch_get_main_queue(), ^{
        [[UIApplication sharedApplication] openURL:[NSURL URLWithString:text] options:@{} completionHandler:nil];
    });
}

void ios::ShowRestartRequired()
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Restart required",
        "Your settings have been saved. Close Marathon Recompiled from the app switcher and open it again to apply them.",
        GameWindow::s_pWindow);
}

int ios::GetThermalState()
{
    return int([NSProcessInfo processInfo].thermalState);
}

std::filesystem::path os::process::GetExecutablePath()
{
    return std::filesystem::path([NSBundle mainBundle].executablePath.fileSystemRepresentation);
}

std::filesystem::path os::process::GetExecutableRoot()
{
    return std::filesystem::path([NSBundle mainBundle].resourcePath.fileSystemRepresentation);
}

std::filesystem::path os::process::GetWorkingDirectory()
{
    return std::filesystem::current_path();
}

bool os::process::SetWorkingDirectory(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::current_path(path, error);
    return !error;
}

bool os::process::StartProcess(const std::filesystem::path&, const std::vector<std::string>&, std::filesystem::path)
{
    ios::ShowRestartRequired();
    return false;
}

void os::process::CheckConsole() { g_consoleVisible = false; }
void os::process::ShowConsole() {}
bool os::media::IsExternalMediaPlaying() { return [AVAudioSession sharedInstance].otherAudioPlaying; }
bool os::user::IsDarkTheme() { return [UITraitCollection currentTraitCollection].userInterfaceStyle == UIUserInterfaceStyleDark; }
os::version::OSVersion os::version::GetOSVersion()
{
    NSOperatingSystemVersion version = [NSProcessInfo processInfo].operatingSystemVersion;
    return { uint32_t(version.majorVersion), uint32_t(version.minorVersion), uint32_t(version.patchVersion) };
}
