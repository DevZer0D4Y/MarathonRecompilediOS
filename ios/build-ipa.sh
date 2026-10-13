#!/usr/bin/env bash
# Build and package an unsigned device IPA on a Mac with full Xcode installed.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
output="$repo_root/out/MarathonRecomp-mods.ipa"
build_args=()
while (( $# )); do
    case "$1" in
        --output)
            if (( $# < 2 )); then echo 'Missing --output path' >&2; exit 1; fi
            output=$2
            shift 2
            ;;
        --help|-h)
            echo 'Usage: bash ios/build-ipa.sh [--game-source /path/to/game] [--output /path/to/app.ipa] [CMake options]'
            echo 'Requires macOS, Xcode, CMake, Ninja and your original Sonic (2006) Xbox 360 inputs.'
            exit 0
            ;;
        *) build_args+=("$1"); shift ;;
    esac
done
if [[ $(uname -s) != Darwin ]]; then
    echo 'An IPA build requires macOS and the full Xcode/iPhoneOS SDK. Run this script on your Mac.' >&2
    exit 1
fi
command -v xcodebuild >/dev/null || { echo 'Install and select full Xcode first.' >&2; exit 1; }
command -v python3 >/dev/null || { echo 'Missing python3' >&2; exit 1; }
xcrun --sdk iphoneos --show-sdk-path >/dev/null
bash "$repo_root/ios/build.sh" "${build_args[@]}"
xcodebuild -project "$repo_root/out/build/ios-device/MarathonRecomp-ALL.xcodeproj" \
    -scheme MarathonRecomp -configuration Release -sdk iphoneos \
    -destination 'generic/platform=iOS' -jobs "${MARATHON_RECOMP_BUILD_JOBS:-2}" \
    CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY= \
    COMPILER_INDEX_STORE_ENABLE=NO build
python3 "$repo_root/ios/package-playtest.py" \
    "$repo_root/out/build/ios-device/MarathonRecomp/Release-iphoneos/MarathonRecomp.app" "$output"
