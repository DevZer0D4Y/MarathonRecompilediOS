#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++20 -O1 -g -pthread -fsanitize=undefined -fno-sanitize-recover=all \
    -DPLUME_SDL_VULKAN_ENABLED -I"$repo_root/thirdparty/SDL/include" -I"$repo_root/MarathonRecomp" -I"$repo_root/thirdparty/plume" "$repo_root/tests/port_regressions.cpp" -o "$test_dir/regressions"
"$test_dir/regressions"

"${CXX:-c++}" -std=c++20 -O1 -g -pthread -fsanitize=undefined -fno-sanitize-recover=all \
    -ffunction-sections -fdata-sections \
    -I"$repo_root/tests/touch_stubs" -I"$repo_root/thirdparty/SDL/include" \
    -I"$repo_root/thirdparty/imgui" -I"$repo_root/tools/XenonRecomp/thirdparty/tomlplusplus/include" \
    -I"$repo_root/MarathonRecomp" "$repo_root/tests/touch_controls_regressions.cpp" \
    "$repo_root/thirdparty/imgui/imgui.cpp" "$repo_root/thirdparty/imgui/imgui_draw.cpp" \
    "$repo_root/thirdparty/imgui/imgui_tables.cpp" "$repo_root/thirdparty/imgui/imgui_widgets.cpp" \
    -o "$test_dir/touch-regressions"
mkdir "$test_dir/layout"
"$test_dir/touch-regressions" "$test_dir/layout"
