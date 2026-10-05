#!/usr/bin/env bash
# Incremental device build of the generated Xcode project, at low priority and with limited jobs.
# Usage: bash ios/dev-build.sh [device UDID]
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
destination=${1:+id=$1}
nice -n 19 xcodebuild -project "$repo_root/out/build/ios-device/MarathonRecomp-ALL.xcodeproj" \
    -scheme MarathonRecomp -configuration Release \
    -destination "${destination:-generic/platform=iOS}" \
    -jobs "${MARATHON_RECOMP_BUILD_JOBS:-2}" -allowProvisioningUpdates \
    COMPILER_INDEX_STORE_ENABLE=NO GCC_GENERATE_DEBUGGING_SYMBOLS=NO DEBUG_INFORMATION_FORMAT=dwarf ONLY_ACTIVE_ARCH=YES \
    build
