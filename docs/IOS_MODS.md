# Mods on iOS

Marathon Recompiled has a native mod manager before the game starts, adapted
from the Unleashed Recompiled iOS port. It manages asset replacements for
**Sonic the Hedgehog (2006) / Marathon Recompiled**.

## Install and select mods

1. Extract the mod archive in Files. Find the folder containing `mod.ini`.
2. Open the app and tap **Import folder**. Select that extracted folder.
   The app copies it into `Documents/MarathonRecomp/mods`; the original is left alone.
3. Tap a mod to enable it. New mods start disabled. An unsupported requirement
   appears below its title and prevents enabling it.
4. Drag rows to set priority. The top enabled mod wins when two mods replace the
   same file. Enable any declared dependencies before tapping **Play**.
5. Select any **Built-in codes** you want, then tap **Play**. Changes apply to
   this launch. Close and reopen the app to change the selection again.

You can also add or remove extracted mod folders in **Files → On My iPhone/iPad →
Sonic (2006) → MarathonRecomp → mods** while the game is closed. Each immediate
child must contain `mod.ini`. Importing a duplicate folder name never overwrites
the existing mod or its saves; rename the source folder to install another copy.

The selection and order are stored in `ios-mods.ini` beside the game data. All
paths in that database are relative, so app updates and iOS container relocation
do not break them. Desktop `cpkredir.ini` settings do not override this selection.

## Asset layout and compatibility

The first version supports complete file replacements through the game's virtual
filesystem, including complete `.arc` archives. Each include directory mirrors
the installed `game` folder. For example:

```text
mods/MyMod/
  mod.ini
  assets/
    xenon/
      archives/
        sonic.arc
```

```ini
[Main]
IncludeDirCount=1
IncludeDir0="assets"
SaveFile="saves"

[Desc]
Title="My Marathon mod"
Author="Your name"
Version="1.0.0"
```

Use the exact relative archive name required by your mod. `sonic.arc` here is a
layout example, not a promise that every mod replaces that archive. Multiple
HMM include directories are supported in their declared order. Xbox-style
`D:\xenon\...` and `game:\xenon\...` accesses resolve against these directories;
ASCII letter case does not affect matching. Missing replacements fall back to
the original game. The original installed files are not rewritten by importing
or selecting a mod.

HMM `SaveFile` names a directory inside the mod. The game uses
`SonicNextSaveData.bin` there, with the existing base-save fallback. UMM `Save`
names a file inside the mod. The first enabled mod declaring a save wins.
Without a declaration, the normal save location is used. UMM file replacement
is supported when `Merge` is disabled.

Dependencies use HMM IDs and minimum dotted numeric versions. Non-numeric
versions require an exact match. Symlinks, escaping include/save paths, and
case-colliding paths inside a package are rejected.

Unleashed mods, `+*.ar`/`+*.arl` append archives, and UMM archive merging are not
implemented for Sonic '06. Replacing individual members of an original `.arc`
requires a separately implemented archive hook; loose files only work when the
game actually opens that file through its filesystem. DLL plugins, custom HMM
`CodeFile` scripts, `.xex`/`.xexp` patches and native libraries require individual
source ports. Mods introducing shaders also need compatibility with the compiled
shader cache. The manager can check declarations and paths, but cannot establish
that every asset is compatible with the game or Metal renderer.

The built-in codes are the boolean codes compiled into Marathon and saved in
`config.toml`. They do not execute downloaded scripts. There is no online catalog,
automatic update/download service or archive extraction; extract before importing.

## Validation and building

From the repository root, after initializing recursive submodules:

```bash
bash tests/run-mod-regressions.sh
bash tests/run-port-regressions.sh
```

The first command runs the portable manager and the production iOS file-overlay
backend under AddressSanitizer and UndefinedBehaviorSanitizer. It checks import
collisions, unsupported declarations, dependency versions, persistence/order,
path confinement, case-insensitive priority, directory searches, reloads, mod
saves and container relocation. The existing source tests cover touch controls,
fade rendering, texture decoding and other portable port behavior.

The macOS CI job also compiles the UIKit manager against the iPhoneOS SDK. This
is a compile check, not a complete app build or a physical-device test.

To build an unsigned IPA **on a Mac with full Xcode selected**, using your own
original game inputs:

```bash
bash ios/build-ipa.sh --game-source "/path/to/extracted/game" \
  --output "out/MarathonRecomp-mods.ipa"
```

The script uses the existing iOS preparation/code-generation flow, builds the
Release arm64 app with signing disabled, then calls `ios/package-playtest.py`.
The IPA must be signed with your sideloading tool or your own identity before
installation. See [IOS.md](IOS.md) for prerequisites and memory entitlements.

For this change, the portable mod and existing port tests passed on Linux. The
Linux environment has no Xcode/iPhoneOS SDK or original game inputs, so no IPA
was built there. The new UIKit code, full app link and physical-device mod loading
still require validation on the Mac. Test import via Files, enable/disable,
conflict priority, missing dependencies, separate saves, no-mod fallback and
relaunch/container persistence with an actual compatible Sonic '06 mod before
distributing the build.
