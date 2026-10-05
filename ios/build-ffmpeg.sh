#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
if [[ $(uname -s) != Darwin ]]; then
    echo 'FFmpeg for iOS must be built on a Mac with Xcode.' >&2
    exit 1
fi
source_dir="$repo_root/out/ios-ffmpeg-source"
source_commit=db69d06eeeab4f46da15030a80d539efb4503ca8
build_dir="$repo_root/out/ios-ffmpeg-build"
install_dir="$repo_root/out/ios-ffmpeg"
if [[ ! -d "$source_dir/.git" ]]; then
    git clone --depth 1 --branch n7.1.1 https://github.com/FFmpeg/FFmpeg.git "$source_dir"
fi
if [[ $(git -C "$source_dir" rev-parse HEAD) != "$source_commit" ]]; then
    echo "FFmpeg source must be n7.1.1 at $source_commit. Found a different revision in $source_dir." >&2
    exit 1
fi
if ! git -C "$source_dir" apply --reverse --check "$repo_root/ios/ffmpeg-xmaframes.patch" 2>/dev/null; then
    git -C "$source_dir" apply --check "$repo_root/ios/ffmpeg-xmaframes.patch"
    git -C "$source_dir" apply "$repo_root/ios/ffmpeg-xmaframes.patch"
fi
sdk_path=$(xcrun --sdk iphoneos --show-sdk-path)
compiler=$(xcrun --sdk iphoneos --find clang)
mkdir -p "$build_dir"
cd "$build_dir"
"$source_dir/configure" --prefix="$install_dir" --target-os=darwin --arch=aarch64 \
    --enable-cross-compile --cc="$compiler" --sysroot="$sdk_path" \
    --extra-cflags='-arch arm64 -miphoneos-version-min=17.0' \
    --extra-ldflags='-arch arm64 -miphoneos-version-min=17.0' \
    --enable-pic --enable-static --disable-shared --disable-programs --disable-doc \
    --disable-autodetect --disable-everything --disable-avdevice --disable-avfilter \
    --enable-decoder=xmaframes
make -j "${MARATHON_RECOMP_BUILD_JOBS:-2}"
make install
