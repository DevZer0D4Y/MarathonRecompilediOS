# iOS source validation

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

## Mac playtest build — 2026-10-05

- Xcode 27.0 (27A266a), iPhoneOS 27.0 SDK, Release arm64 deployment target iOS 17.0: app compiled and linked successfully.
- Used the existing local generated CPU code and guest shader cache from the original game inputs. The PC share was not mounted during this build; no new source-game copies were made.
- All portable undefined-behavior-sanitized port, touch-control and production fade regressions pass on Apple Silicon. The allocation test now respects the host page size.
- BC textures decode directly into the shared Metal upload buffer, removing the extra full-size CPU image allocation and copy. The output matches the old decoder across BC1–BC7, signed formats, HDR, cropped mips and volume slices. Destination-size and layout-overflow checks pass.
- Local CPU upload microbenchmark (1024×1024, seven batches of twelve iterations): BC1 median 1.666 → 1.522 ms; BC3 2.161 → 1.930 ms. These are host CPU measurements, not iPhone FPS results.
- Performance-only counters use relaxed atomic operations. They do not participate in thread synchronization.
- The playtest IPA contains only the executable, Info.plist, PkgInfo, asset catalog and app icons. It preserves the project identity (`com.devz.sonic2006`, displayed as `Sonic (2006)`), has no code signature or provisioning profile, and no player Documents, original game archives, saves, configuration or logs. Shader helper debug source and local build paths were removed. Package integrity and file hashes are checked by ios/package-playtest.py.
- The same optimized build was signed locally and installed through Xcode Device Hub on the connected iPhone 14 Pro Max on 2026-10-05. The local update preserves the existing `com.devz.marathonrecomp` identity and signing team, without uninstalling the app; Device Hub confirmed that `Marathon Recompiled` was replaced by `Sonic (2006)`, version 1.0.0 (staged build 1.0.5). The unsigned tester IPA retains `com.devz.sonic2006` and contains no personal signing material. Sustained FPS, thermal behavior, memory use and full gameplay validation remain outstanding. Testers must sign the IPA with their own identity and import their own game data through the installer.
