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

The manager supports complete file replacements and Sonic '06 U8 `.arc`
merging through the game's virtual filesystem. Each include directory mirrors
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
Without a declaration, the normal save location is used.

Dependencies use HMM IDs and minimum dotted numeric versions. Non-numeric
versions require an exact match. Symlinks, escaping include/save paths, and
case-colliding paths inside a package are rejected.

### Archive merging

UMM `[Details]` or `[Filesystem]` `Merge=true` enables archive merging. HMM
packages can opt in with `[Main] Merge=true`. The setting now performs a real
Sonic '06 archive merge instead of rejecting the mod. Supported layouts are:

- A partial packed `.arc` at the original archive's relative path.
- An extracted directory named `example.arc` at that same path, with member
  paths below it. An extensionless `example` directory is also recognized when
  the installed game contains `example.arc` beside that location.
- Loose files matching existing archive member paths, optionally prefixed by
  the archive's platform directory (for example `xenon/`). The member index also
  includes new members supplied by another enabled partial archive.

For example, `xenon/archives/example.arc/scripts/player.lua` in an extracted
archive folder replaces `scripts/player.lua` inside that game's archive.
A loose `scripts/player.lua` can replace that member in every matching archive.
New members can be added through packed or extracted archive patches.

The reader accepts extended Sonic '06 U8 nodes and ordinary 12-byte raw U8
nodes, validating the complete table before selecting a layout. Root labels and
unused directory metadata do not affect member lookup. Generated files always
use the extended Sonic '06 layout with its tool compatibility marker; raw U8
input is converted rather than passed to the game unchanged. Errors distinguish
installed originals, complete replacements and mod archives.

Untouched members, including their original zlib-compressed bytes, are retained.
Matching ignores ASCII case, preserves existing member spelling, and applies
mods from bottom to top so the top enabled mod wins each member conflict.
A complete archive from a `Merge=false` mod cuts off the original and lower
mods for that archive. UMM's comma-separated `Read-only` list likewise treats
those relative archive paths as complete replacements. Higher merging mods can
still patch that replacement.

Archives are prepared in the background before game startup with bounded payload
buffers. The manager shows **Preparing mods…** and stays open on an error, so
you can change the selection and retry. The
installed game and imported mod files remain untouched. Generated archives live
in `mod-cache` beside `ios-mods.ini`; content fingerprints invalidate them after
asset changes even if size and modification date stay the same. Repeated launches
reuse verified cache entries. First preparation needs extra storage for merged
archives and can take longer. You may remove `mod-cache` while the game is closed
to reclaim space; it is rebuilt when needed. Invalid or conflicting archive
structures stop preparation with an error instead of launching a partial merge.

This supports Sonic '06 asset formats, not every executable modification.
Unleashed `.ar`/`.arl` archives, DLL plugins, custom HMM `CodeFile` scripts,
`.xex`/`.xexp` patches and native libraries require individual source ports.
Mods introducing shaders also need compatibility with the compiled shader cache.
The manager cannot establish that every mod asset is compatible with the game
or Metal renderer.

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
saves and container relocation. Archive tests cover independent U8/zlib decoding,
packed/extracted/loose patches, conflict priority, full-replacement boundaries,
content cache invalidation, malformed inputs and failed-reload rollback.
`ARC_TEST_REAL_ARCHIVE=/path/to/shader.arc` adds a real-game archive round trip;
`ARC_TEST_MOD_FOLDER=/path/to/mod` tests a packed mod through import, selection
and merging against synthetic originals, comparing every decoded patch member.
These optional tests read the supplied inputs without modifying them. The existing source tests cover touch controls,
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

A successful portable test or app build does not establish physical-device mod
compatibility. Test import via Files, enable/disable, conflict priority, missing
dependencies, separate saves, no-mod fallback and relaunch/container persistence
with compatible Sonic '06 mods on the target device.
