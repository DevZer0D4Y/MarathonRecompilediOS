# Accept either the upstream flat private/ layout or an untouched extracted
# Xbox 360 game folder, including a read-only network share mounted on macOS.
get_filename_component(MARATHON_RECOMP_GAME_INPUT_DIR "${MARATHON_RECOMP_GAME_INPUT_DIR}" ABSOLUTE)
set(MARATHON_RECOMP_XEX_INPUT "${MARATHON_RECOMP_GAME_INPUT_DIR}/default.xex")
foreach(archive shader shader_lt)
    if(EXISTS "${MARATHON_RECOMP_GAME_INPUT_DIR}/${archive}.arc")
        set(MARATHON_RECOMP_${archive}_INPUT "${MARATHON_RECOMP_GAME_INPUT_DIR}/${archive}.arc")
    else()
        set(MARATHON_RECOMP_${archive}_INPUT "${MARATHON_RECOMP_GAME_INPUT_DIR}/xenon/archives/${archive}.arc")
    endif()
endforeach()

if(DEFINED MARATHON_RECOMP_GENERATED_DIR)
    cmake_path(IS_PREFIX MARATHON_RECOMP_GAME_INPUT_DIR "${MARATHON_RECOMP_GENERATED_DIR}" NORMALIZE GENERATION_WRITES_GAME_SOURCE)
    if(GENERATION_WRITES_GAME_SOURCE)
        message(FATAL_ERROR "MARATHON_RECOMP_GENERATED_DIR must be outside the original game input directory.")
    endif()
endif()
