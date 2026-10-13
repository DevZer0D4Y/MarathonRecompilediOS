#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -g -DXXH_INLINE_ALL \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$repo_root/MarathonRecomp" \
    -I"$repo_root/thirdparty/unordered_dense/include" \
    -I"$repo_root/tools/XenonRecomp/thirdparty/xxHash" \
    "$repo_root/MarathonRecomp/mod/ios_mod_manager.cpp" \
    "$repo_root/tests/ios_mod_manager_tests.cpp" -o "$test_dir/mod-regressions"
"$test_dir/mod-regressions" "$test_dir/fixtures"
