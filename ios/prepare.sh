#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
apply_once() {
    local directory=$1 patch_file=$2
    if git -C "$directory" apply --reverse --check "$patch_file" 2>/dev/null; then
        return
    fi
    git -C "$directory" apply --check "$patch_file"
    git -C "$directory" apply "$patch_file"
}
apply_once "$repo_root/thirdparty/plume" "$repo_root/ios/patches/plume-ios.patch"
apply_once "$repo_root/tools/XenosRecomp" "$repo_root/ios/patches/xenos-ios-sdk.patch"
