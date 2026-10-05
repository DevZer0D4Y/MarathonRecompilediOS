<p align="center">
    <img src="https://raw.githubusercontent.com/IsaacMarovitz/MarathonRecompResources/refs/heads/main/images/logo/Logo.png" width="512"/>
</p>

---

> [!CAUTION]
> This port is experimental. It has been played on an iPhone 14 Pro Max, but full playthroughs of all three episodes, long sessions and other devices have not been tested yet.

Marathon Recompiled for iOS is an unofficial iOS port of the Xbox 360 version of Sonic the Hedgehog (2006) created through the process of static recompilation. It is based on [Marathon Recompiled](https://github.com/sonicnext-dev/MarathonRecomp) and brings it to iPhone and iPad with touch controls, Metal rendering and an installer that works with the Files app.

**This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build Marathon Recompiled for iOS.**

[XenonRecomp](https://github.com/sonicnext-dev/XenonRecomp) and [XenosRecomp](https://github.com/sonicnext-dev/XenosRecomp) are the main recompilers used for converting the game's original PowerPC code and Xenos shaders into compatible C++ and HLSL code respectively.

If you want to support me and remain up-to-date about this and other projects, you can in different ways:

Ko-Fi: https://ko-fi.com/dev_zer0

Discord: https://discord.gg/uFChheZEWX

YouTube: https://www.youtube.com/@develop_erZ

## Table of Contents

- [Minimum System Requirements](#minimum-system-requirements)
- [How to Install](#how-to-install)
- [What's New in This Port](#whats-new-in-this-port)
- [Known Issues](#known-issues)
- [FAQ](#faq)
- [Building](#building)
- [Credits](#credits)

## Minimum System Requirements

- Device:
  - iPhone or iPad with a 64-bit Apple chip and Metal support.
- Memory:
  - 6 GB of RAM recommended (e.g. iPhone 13 Pro, iPhone 14 Pro or newer). Devices with less RAM may be closed by iOS when memory runs low, unless your signing certificate allows the extra memory permissions (see [the FAQ](#the-app-closes-on-launch-or-after-a-while)).
- Operating System:
  - iOS / iPadOS 17.0 or newer.
- Controls:
  - Built-in touch controls, or an MFi, Xbox or PlayStation controller (recommended).
- Storage:
  - Enough free space for the full game, plus the app itself.

> [!NOTE]
> Extra space is needed temporarily if you copy a `.zip` or another archive to your device before extracting the game files.

## How to Install

1) You must have a copy of **Sonic the Hedgehog (2006) for Xbox 360**, extracted to a folder (the folder that contains `default.xex` and the `xenon` folder). Disc images can be extracted with tools such as [extract-xiso](https://github.com/XboxDev/extract-xiso). DLC is optional.

2) Download the `.ipa` from [the latest release](../../releases/latest) and install it with your sideloading tool of choice (AltStore, SideStore, Sideloadly, ESign, ...). The `.ipa` is unsigned, so your tool will sign it with your own certificate.

3) Open **Sonic (2006)**. The installer will start and ask for your game files:

    - **Add Folder** lets you pick the **extracted game folder** with the Files picker. It can be in **On My iPhone**, iCloud Drive, a USB drive or a network share (Files → **Connect to Server**).
    - **Add Files** lets you pick **containers or images dumped from an Xbox 360**.

4) The installer checks the files and copies them into the app's own folder. After that the game starts.

### Installing from a network share or a computer

If the Files picker can't open your network share, copy the extracted game folder into the app's folder first and pick that copy in the installer:

- **iPhone only:** in the **Files** app go to **On My iPhone** → **Sonic (2006)** → **MarathonRecomp** and paste the folder there.
- **With a Mac or PC:** connect the device and open it in Finder (or the Apple Devices app on Windows), go to **Files** → **Sonic (2006)**, and drag the folder in.

You can delete the copied folder once the installer has finished.

> [!NOTE]
> It is **not possible** to complete the installation if your files have been **modified**. Discs and Title Updates from different regions can't be used together.

## What's New in This Port

A list of everything fixed and added in this iOS port, on top of the original Marathon Recompiled.

### New Features

| Feature | What it does |
|---|---|
| **iOS and iPadOS support** | The game runs natively on iPhone and iPad through SDL's UIKit backend and Metal, without JIT. |
| **Installer with the Files app** | The installer uses Apple's Files picker, so the game can be installed from local storage, iCloud Drive, USB drives or network shares. Game data, settings and saves live in the app's own folder, visible in the Files app and Finder. |
| **Touch controls** | The on-screen gamepad from [Unleashed Recompiled for iOS](https://github.com/DevZer0D4Y/UnleashedRecompilediOS): a stick with a D-Pad ring, swipe anywhere to move the camera, all face, shoulder and trigger buttons, Start, Back and haptic feedback. They hide when you use a controller and come back when you touch the screen. See [What are the controls on iOS?](#what-are-the-controls-on-ios). |
| **Touch layout editor** | Tap **EDIT** to move, resize and hide controls and change their transparency. Layouts are saved to `touch_layout.toml`. |
| **Mobile graphics defaults** | MSAA off, 1024 shadows, quarter-resolution reflections and 4x anisotropic filtering by default, so the game runs smoothly on a phone. All of them can still be raised in the options menu. |

### iOS Fixes

| Problem | Fix |
|---|---|
| **Black screen and crash on launch on recent iOS** | Recent iOS versions require apps to use scenes. The app now has a scene manifest and waits for its scene before drawing anything. |
| **Crash when opening the app** | A controller hint asked for Bluetooth access without a permission prompt, which made iOS close the app. It is no longer used on iOS. |
| **Broken textures** | iPhones can't read the Xbox's compressed texture formats (BC1 to BC7). They're now converted while loading, keeping mipmaps and sRGB colours. |
| **No sound in cutscenes and music** | The Xbox's XMA audio is decoded by a custom FFmpeg decoder, built for iOS and linked into the app. |
| **Low frame rate** | The renderer ended and restarted its Metal render pass at every resource barrier, about 36 times per frame. It now keeps the pass open when it can, and the game's per-surface MSAA is capped by the Anti-Aliasing setting. Soleanna town went from about 20 FPS to 53-60 FPS on an iPhone 14 Pro Max. |
| **Slow startup and calls** | About 21,700 symbols were exported from the game code, each going through a dynamic linker stub. They are now hidden, so calls go directly. |
| **Surround sound in the wrong speakers** | 5.1 audio channels are now interleaved in the right order. |
| **Random freezes** | Thread waits now have proper timeouts instead of being able to hang forever. |
| **Crash when memory runs out** | Failed memory allocations are handled instead of crashing. |
| **Fades not drawn or stuck** | Screen fades are drawn and finish at the right time. |
| **Going to the background** | Held touch inputs are released when the app goes to the background, so Sonic doesn't keep running when you come back. |
| **Restarting the game did nothing** | iOS apps can't relaunch themselves. Settings that need a restart are saved, and the game asks you to close and reopen it. |

### Can't Be Fixed From This Project

| Issue | Why |
|---|---|
| **Bugs in the original game** | The port keeps the original game's behaviour, like upstream Marathon Recompiled. |
| **PlayStation 3 copies of the game** | They would need their own recompilation. |

## Known Issues

- Full playthroughs of all three episodes have not been tested on iOS yet.
- The phone heats up in long sessions, and iOS may lower the frame rate when it does.
- On devices with less than 6 GB of RAM, iOS may close the game. See [the FAQ](#the-app-closes-on-launch-or-after-a-while).

Before reporting a bug, check whether it also happens in [Marathon Recompiled](https://github.com/sonicnext-dev/MarathonRecomp/issues) or on original Xbox 360 hardware.

## FAQ

### What are the controls on iOS?

The game has on-screen touch controls:

- **Left stick:** drag inside the circle on the bottom left. Tap the arrows on its outer ring for the D-Pad.
- **Camera (right stick):** swipe anywhere on the screen that isn't a button.
- **A, B, X, Y:** the diamond of buttons on the right.
- **LT, RT:** the wide buttons above the stick and above the face buttons.
- **LB, RB, BACK, START:** along the top of the screen.

A controller (MFi, Xbox or PlayStation) is still the best way to play. The touch controls hide themselves as soon as you use a controller, and come back when you touch the screen.

#### Customising the touch controls

Pause the game, then tap **EDIT** at the top of the screen to open the layout editor:

- **Move** a control by dragging it.
- **Resize** it by tapping it to select it, then using **SIZE -** and **SIZE +**.
- **Hide** a control you don't need by selecting it and tapping **HIDE**. Hidden controls stay faintly visible in the editor, so you can select them and tap **SHOW** to bring them back.
- **ALPHA -** and **ALPHA +** make all the controls more or less see-through.
- **RESET** restores the default layout.
- **DONE** saves your layout and goes back to the game.

Your layout is saved to `touch_layout.toml`, in the same folder as `config.toml`. Delete that file to go back to the default layout. The touch controls can also be turned off with `TouchControls = false` in `config.toml`.

### Where is the save data and configuration file stored?

In the app's folder: **Files** → **On My iPhone** → **Sonic (2006)** → **MarathonRecomp**. Save data is in the `save` folder, and the configuration file is `config.toml`. The installed game is in the `game` folder.

### I want to update the app. Will I lose my save data?

No. Install the new `.ipa` over the old one with the same sideloading tool, without deleting the app first. Your game files, saves and settings stay in place. Deleting the app deletes its folder too, so back up the `save` folder first if you do.

### The app closes on launch or after a while

The game reserves a large amount of memory. Apple only lets apps use more than the usual limit if their signing certificate allows the **Extended Virtual Addressing** and **Increased Memory Limit** permissions, which free Apple accounts can't grant. Devices with 6 GB of RAM or more run the game fine without them. On devices with less RAM, sign the app with a paid developer certificate that has these permissions, or close other apps before playing.

### Why does the installer say my files are invalid?

- Make sure you picked the **extracted** game folder (with `default.xex` inside), not a `.zip`, `.7z` or `.rar`.
- **Add Folder** does not search inside your folder for the game. Pick the folder that directly contains the game's files.
- The installer only accepts **original and unmodified files** from the same region.

### Can I install the game with a PlayStation 3 copy?

**You cannot use the files from the PlayStation 3 version of the game.** Supporting them would need an entirely new recompilation. All significant differences present in the PS3 version are included in Marathon Recompiled as options.

## Building

The app is built on a Mac with Xcode. The build needs `default.xex`, `shader.arc` and `shader_lt.arc` from your own copy of the game, and reads them in place:

```bash
git clone --recurse-submodules <this repository>
cd MarathonRecomp
bash ios/build.sh --game-source "/path/to/extracted/game"
```

Then open `out/build/ios-device/MarathonRecomp-ALL.xcodeproj`, pick your signing team and device, and build **MarathonRecomp**. See [docs/IOS.md](/docs/IOS.md) for signing options, the regression tests and keeping the game files on another computer ([docs/PC_TO_IPHONE.md](/docs/PC_TO_IPHONE.md)).

To make an unsigned `.ipa` like the release one, which contains only the app and no signing data:

```bash
python3 ios/package-playtest.py out/build/ios-device/MarathonRecomp/Release-iphoneos/MarathonRecomp.app MarathonRecomp.ipa
```

## Credits

### Marathon Recompiled
- [ga2mer](https://github.com/ga2mer): Creator of the recompilation, laying the initial foundation for the project. Other responsibilities include maintaining the audio backend and providing various patches for the game.

- [IsaacMarovitz](https://github.com/IsaacMarovitz): Graphics Programmer for the recompilation. Other responsibilities include maintaining macOS support and aiding in porting mod manager patches to the recompilation.

- [squidbus](https://github.com/squidbus): Graphics Programmer for the recompilation. Aided in researching the game's internals.

- [Hyper](https://github.com/hyperbx): Developer of system level features, such as achievement support and the custom menus, alongside various other patches and features to make the game feel right at home on modern systems. Aided in researching the game's internals.

- [Rei-san](https://github.com/ReimousTH): Developer of quality of life patches and extensive amounts of research into the game's internals.

- [Desko](https://github.com/FateForWindows): Aided in researching the game's internals and created many quality of life patches for the original game used by the recompilation.

- [LJSTAR](https://github.com/LJSTARbird): Artist behind the project logo. Provided French localization for the custom menus.

- [brianuuuSonic](https://github.com/brianuuu): Provided Japanese localization for the custom menus.

- [Kitzuku](https://github.com/Kitzuku): Provided German localization for the options menu.

- Ray Vassos: Provided German localization for the achievements menu.

- [DaGuAr](https://x.com/TheDaguar): Provided Spanish localization for the custom menus.

- [NextinHKRY](https://github.com/NextinMono): Provided Italian localization for the custom menus.

- [Hotline Sehwani](https://www.youtube.com/channel/UC9NBX5UPq4fYvbr7bzvRvOg) & SilverIceSound: Produced the [installer music](https://www.youtube.com/watch?v=8mfOSTcTQNs) ([original prod.](https://www.youtube.com/watch?v=k_mGNwrxR5M) by [Tomoya Ohtani](https://www.youtube.com/@TomoyaOhtaniChannel)).

### iOS Port
- [DevZer0D4Y](https://github.com/DevZer0D4Y): iOS port, touch controls (from Unleashed Recompiled for iOS), Metal performance fixes and iOS packaging.

### Special Thanks
- Unleashed Recompiled Development Team: Created much of the ground work that made all of this possible and sped up development time considerably.

- [Skyth](https://github.com/blueskythlikesclouds): Provided graphics consultation and support for dynamic aspect ratio.

- [Darío](https://github.com/DarioSamo): Creator of the graphics hardware abstraction layer [plume](https://github.com/renderbag/plume), used by the project's graphics backend.

- [Syko](https://x.com/UltraSyko): Aided in identifying fonts used by the original SonicNext logo.

- [ocornut](https://github.com/ocornut): Creator of [Dear ImGui](https://github.com/ocornut/imgui), which is used as the backbone of the custom menus.

- Raymond Chen: Useful resources on Windows application development with his blog ["The Old New Thing"](https://devblogs.microsoft.com/oldnewthing/).
