# iOS source validation

## Launch fix — 2026-10-14

The October 13 IPA was reproduced crashing on an iPhone 14 Pro Max running iOS 27.0.1. Its crash report faults in `___UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption_block_invoke`. The previously omitted scene lifecycle implementation from repository main commit `146aeed` is now included in the app, declared in the bundle, awaited before window creation and attached to SDL's window.

A follow-up device launch exposed a second startup abort in `__CBFRAMEWORK_CRASHING_DUE_TO_PRIVACY_VIOLATION__`. Controller discovery requires `NSBluetoothAlwaysUsageDescription`; that purpose string is now included. The packager verifies the scene manifest, linked scene delegate and Bluetooth purpose string, rejecting the old release bundle before creating an IPA.

The final Release app built and signed successfully using an isolated test identifier and with extended-memory entitlements disabled for the personal development team. It launched without either abort and was confirmed running by two process snapshots after the second launch, with no additional app crash report. UIKit/Metal/controller initialization ran. These are startup checks; game installation, gameplay and mod import/selection were not exercised. The public unsigned package retains `com.devz.MarathonRecomp` and excludes signing/provisioning data. No original game inputs are included.

`MARATHON_RECOMP_IOS_EXTENDED_MEMORY` remains enabled by default; setting it OFF permits signing a test build with a personal team that cannot provision that entitlement. The unsigned release needs recipient signing as before.


## macOS update — 2026-10-13

The native iOS mod manager and Sonic 2006 file overlays from cloud commit `facbd8e586ccb88c9a186dae9e81bb9775165c77` are integrated in local commit `a8fce5edbdb9f02f076a999056bdcf1285260ad1`, preserving Dev Zero's author and author date. The local integration branch is `ios-mod-integration-20261013`.

Xcode 27.0 (27A266a), the iPhoneOS 27.0 SDK and Metal compiler successfully built and linked the Release arm64 app for iOS 17+. Original `default.xex`, `shader.arc` and `shader_lt.arc` supplied under Downloads were used for CPU/shader generation and retained unchanged by SHA-256 comparison. The unsigned IPA was packaged and its ZIP integrity, arm64 architecture, missing signing/provisioning data and absence of original game input files were verified. The linked executable contains the native manager entry point and import/scan/overlay symbols.

Build fixes cover Bash 3.2 empty arrays, Xcode ordering for shared generated sources, native DXC visibility for helper shaders, and the SDL termination event name. The packaging helper required by the patch was also added. A disk-full build failure was resolved after clearing disposable dependency/Homebrew caches and resuming compilation.

Mod regressions passed with ASan/UBSan. Portable port, touch and production fade regressions passed with UBSan; upstream warnings remain. Read-only game-input regressions passed, including after the CMake build-order changes. No physical device installation, gameplay, Files picker interaction, rendering, audio or performance tests were performed in this session. A successful build does not establish gameplay readiness.

The sections below retain the original October 3 validation history.


Validation date: 2026-10-03. Base: `sonicnext-dev/MarathonRecomp` commit `04431570d1ad56eaf3dda9ddd9f0efc628c455f5`.

## Checks completed in this workspace

- Both portable regression executables pass with the undefined-behavior sanitizer. They exercise the imported production touch controls, simultaneous movement/action inputs, stick deadzone, D-pad, swipe decay, quick taps, controller switching, reset, editing/persistence, malformed layouts, thread completion timeouts, six-channel audio interleaving, release parsing, allocation failure, and BC texture decoding.
- The production fade drawing code passes duration, callback delay, zero-duration fade-in, and callback reentry checks using real ImGui draw data.
- `file_to_c`, `u8extract`, `XenonRecomp`, and `XenosRecomp` compile and link as Linux host tools using Clang through Zig. GCC is unsuitable for some of the upstream C++ aggregates. Apple's compiler remains required for the Mac build.
- A `file_to_c` output compiles as C and reproduces all 256 input byte values. A synthetic archive with raw and zlib-compressed entries extracts to the original data.
- FFmpeg n7.1.1, commit `db69d06eeeab4f46da15030a80d539efb4503ca8`, builds as static Linux libraries with the project's custom XMA patch. A linked probe finds the `AV_CODEC_ID_XMAFRAMES` decoder. Real game audio was not decoded.
- CMake preset JSON and bundle/entitlement plists parse, shell scripts pass syntax checks, and dependency patch preparation is idempotent.
- The iOS build script refuses a Linux app build with instructions to use macOS/Xcode.
- The read-only input regression uses synthetic archives in both flat and extracted-game layouts, including paths with spaces and parentheses. It builds the actual CMake shader-extraction target, confirms extracted bytes in the separate output directory, verifies source hashes and modification times remain unchanged, and checks rejection of missing/empty inputs and generated-output overlap with the original game directory. This verifies local read-only input behavior, not an actual SMB connection.

The macOS CI job has been added but has not been run remotely. Portable touch tests simulate SDL host events; they do not exercise UIKit or device haptics. Texture tests exercise the CPU decoder; they do not establish correct GPU rendering on Apple hardware.

## Validation still required

Original game inputs were not accessible from this workspace. No Apple SDK, signing profile, or physical iOS device was available. Therefore the CPU/game shader generation, iPhoneOS compilation, code signing, app launch, Files picker, Metal rendering, game audio playback, performance, memory use, and complete gameplay remain unverified. No playable IPA has been produced. The supplied Windows path identifies where the originals reside; no PC-to-MacBook or PC-to-iPhone transfer has occurred from this session.

Follow [the iOS build instructions](IOS.md), then run installation, menus, all three episodes, loading transitions, saves/reloads, touch/controller interactions, interruptions, and sustained performance on a physical device before publishing a playable release. This source snapshot is experimental and is not a release readiness certification.
