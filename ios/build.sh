#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"
game_input_dir="$repo_root/MarathonRecompLib/private"
device_cmake_args=()
while (( $# )); do
    case "$1" in
        --game-source)
            if (( $# < 2 )); then
                echo 'Usage: --game-source /path/to/read-only/game/folder' >&2
                exit 1
            fi
            game_input_dir=$2
            shift 2
            ;;
        --help|-h)
            echo 'Usage: bash ios/build.sh [--game-source /path/to/game/folder] [CMake signing options]'
            echo 'The source folder may be an extracted Xbox game or the flat upstream private/ layout.'
            echo 'Original files are read in place; generated files stay under this project.'
            exit 0
            ;;
        *)
            device_cmake_args+=("$1")
            shift
            ;;
    esac
done
if [[ $(uname -s) != Darwin ]]; then
    echo 'The iOS app requires a Mac with Xcode and the iPhoneOS SDK. Portable regression checks: bash tests/run-port-regressions.sh' >&2
    exit 1
fi
for command in cmake ninja xcrun; do
    command -v "$command" >/dev/null || { echo "Missing $command" >&2; exit 1; }
done
if [[ ! -d "$game_input_dir" ]]; then
    echo "Game source folder is unavailable: $game_input_dir. Mount the PC share first; see docs/IOS.md." >&2
    exit 1
fi
game_input_dir=$(cd "$game_input_dir" && pwd -P)
cmake "-DMARATHON_RECOMP_GAME_INPUT_DIR=$game_input_dir" -P cmake/CheckGameInputs.cmake
xcrun --sdk iphoneos --show-sdk-path >/dev/null
bash ios/prepare.sh
if [[ ! -x thirdparty/vcpkg/vcpkg ]]; then
    bash thirdparty/vcpkg/bootstrap-vcpkg.sh -disableMetrics
fi
if [[ ! -f out/ios-ffmpeg/lib/libavcodec.a ]]; then
    bash ios/build-ffmpeg.sh
fi
cmake --preset ios-host-tools "-DMARATHON_RECOMP_GAME_INPUT_DIR=$game_input_dir"
cmake --build out/build/ios-host-tools --target file_to_c u8extract marathon_codegen --parallel "${MARATHON_RECOMP_BUILD_JOBS:-2}"
cmake --preset ios-device "${device_cmake_args[@]}" "-DMARATHON_RECOMP_GAME_INPUT_DIR=$game_input_dir"
echo 'Generated out/build/ios-device/MarathonRecomp-ALL.xcodeproj. Open it in Xcode, choose your signing team and an iPhone/iPad, then build MarathonRecomp.'
