# iOS port

This is an experimental source port for arm64 iPhones and iPads running iOS 17 or later. It uses SDL's UIKit entry point and Metal. It does not require JIT. A signed, playable IPA has **not** been built or tested in this Linux workspace. The game files, Xcode SDK, signing profile, and a device are required to finish validation.

## Build on a Mac

Install Xcode (including the Metal compiler tools), CMake 3.28 or newer, and Ninja. Select the full Xcode installation with `xcode-select`; the command-line tools alone do not contain the iPhoneOS SDK. On an Apple Silicon Mac, the host tools and iOS app are both arm64, but they are separate builds with different SDKs.

Clone with `--recurse-submodules`. By default, the build reads these original Xbox 360 files from `MarathonRecompLib/private/`:

- `default.xex`
- `shader.arc`
- `shader_lt.arc`

These are the same inputs required by upstream's [build instructions](BUILDING.md). No game files are included in this port.

To leave the originals on a Windows PC, mount the extracted game folder as a read-only SMB share on the MacBook, keep this repository on the MacBook's local disk, and pass that mounted folder directly:

```bash
bash ios/build.sh --game-source "/Volumes/MarathonSource"
```

The folder must contain `default.xex` at its root and `shader.arc`/`shader_lt.arc` either at its root or under `xenon/archives`. The build reads those files in place. Extracted shader inputs, generated code, shader caches, dependencies, and build products stay in the local project; nothing is written into the game source folder. See [the PC-to-MacBook-to-iPhone workflow](PC_TO_IPHONE.md) for setup and transfer.

Run from the repository root:

```bash
bash ios/build.sh
```

This applies the checked-in dependency patches, builds static FFmpeg with the project's custom XMA decoder, builds the native recompilers, generates the CPU code and iPhoneOS shader cache, and creates the Xcode project. Open `out/build/ios-device/MarathonRecomp-ALL.xcodeproj`, select **MarathonRecomp**, choose your signing team and an iPhone/iPad, then build and run.

To configure signing from the script:

```bash
bash ios/build.sh -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=YOUR_TEAM_ID \
  -DMARATHON_RECOMP_IOS_BUNDLE_ID=your.identifier.MarathonRecomp
```

The app requests `extended-virtual-addressing` for the guest address space and `increased-memory-limit`. Your provisioning profile must allow those entitlements. Memory requirements and performance have not been measured on a device. Start with a recent device with ample RAM and conservative graphics settings. Simulator builds are not configured.

The Mac host binaries are under `out/build/ios-host-tools/bin`. Generated CPU and guest-shader sources, including extracted build-time shader inputs, are under `out/generated/ios`. Helper shaders are generated in the target build directory, so desktop Metal libraries cannot accidentally be reused in the iOS build. macOS FFmpeg archives and DXC dylibs are not linked into the app.

## Install game data and use the controls

The app stores game data, configuration, saves, and `touch_layout.toml` in **Documents/MarathonRecomp**. This directory is accessible through Files and Finder file sharing. The installer uses Apple's Files picker for original source files or extracted folders, holding security-scoped access until installation finishes. You can also transfer an already installed desktop data directory with its `game`, `dlc`, and save/configuration files into this directory.

The touch controls are adapted from [DevZer0D4Y/UnleashedRecompilediOS](https://github.com/DevZer0D4Y/UnleashedRecompilediOS), commit `d42b6de6b77e7abdccd2674ec13fbdef2e0bd4bc`:

- Movement stick with a D-pad ring and swipe camera outside the controls.
- A/B/X/Y, shoulders, triggers, Start, and Back.
- Haptics and a short hold for quick taps at low frame rates.
- **EDIT** opens the layout editor. Drag controls, adjust size/opacity, hide/show them, reset, and use **DONE** to save.
- Using a physical controller hides the overlay; touching the screen restores it.

`TouchControls` and `TouchControlsOpacity` can also be changed in `config.toml`. The installer uses direct touch interaction instead of the gamepad overlay. Captured inputs clear when the app backgrounds. Settings that require restarting are saved and show instructions to close and reopen the app; iOS does not launch a replacement process.

## Validation

See [the validation record](IOS_VALIDATION.md) for completed checks and their limits.

Run the checks that do not need game files:

```bash
bash tests/run-port-regressions.sh
```

They exercise the production touch-control implementation with simulated SDL events, layout editing and persistence, controller switching, input reset, thread wait deadlines, surround channel order, release-version parsing, allocation failure, fade drawing and callback timing, and compressed texture decoding. The tests use the undefined-behavior sanitizer. The host tools can be built without game files:

```bash
bash ios/prepare.sh
cmake --preset ios-host-tools
cmake --build out/build/ios-host-tools --target file_to_c u8extract XenonRecomp XenosRecomp --parallel
python3 tests/game_inputs_regressions.py --u8extract out/build/ios-host-tools/bin/u8extract
```

These checks do not establish gameplay or device readiness. Before releasing a playable build, run the app on a signed physical device and verify installation, menus, all three episodes, loading transitions, audio, saving/reloading, simultaneous movement/action/camera inputs, haptics, layout persistence, controller reconnects, interruptions/backgrounding, and sustained memory/thermal behavior. Use the original game as the comparison for gameplay bugs; upstream deliberately preserves many original behaviors.

BC textures are decoded to uncompressed Metal textures on this port. This covers the embedded UI and game texture formats, preserving mip/slice layout and sRGB sampling, but increases texture memory and upload work. Device profiling will determine whether a hardware compression path is preferable on supported devices.
