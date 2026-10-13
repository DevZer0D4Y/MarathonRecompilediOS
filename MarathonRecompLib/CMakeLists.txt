project("MarathonRecompLib")

add_compile_options(
    -fno-strict-aliasing
)

if (WIN32)
    add_compile_options(/fp:strict)
else()
    add_compile_options(-ffp-model=strict)
endif()

file(MAKE_DIRECTORY "${MARATHON_RECOMP_GENERATED_DIR}/ppc" "${MARATHON_RECOMP_GENERATED_DIR}/shader")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/config/Marathon.toml" RECOMP_CONFIG)
# XenonRecomp prefixes these strings with the configuration directory; absolute
# paths would therefore become invalid concatenations on the host.
file(REAL_PATH "${CMAKE_CURRENT_BINARY_DIR}" RECOMP_CONFIG_DIR)
file(REAL_PATH "${MARATHON_RECOMP_XEX_INPUT}" RECOMP_XEX_PATH)
file(REAL_PATH "${MARATHON_RECOMP_GENERATED_DIR}/ppc" RECOMP_OUTPUT_PATH)
file(REAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config/switch_table.toml" RECOMP_SWITCH_PATH)
file(RELATIVE_PATH RECOMP_XEX_PATH "${RECOMP_CONFIG_DIR}" "${RECOMP_XEX_PATH}")
file(RELATIVE_PATH RECOMP_OUTPUT_PATH "${RECOMP_CONFIG_DIR}" "${RECOMP_OUTPUT_PATH}")
file(RELATIVE_PATH RECOMP_SWITCH_PATH "${RECOMP_CONFIG_DIR}" "${RECOMP_SWITCH_PATH}")
string(REPLACE "../private/default.xex" "${RECOMP_XEX_PATH}" RECOMP_CONFIG "${RECOMP_CONFIG}")
string(REPLACE "../ppc" "${RECOMP_OUTPUT_PATH}" RECOMP_CONFIG "${RECOMP_CONFIG}")
string(REPLACE "./switch_table.toml" "${RECOMP_SWITCH_PATH}" RECOMP_CONFIG "${RECOMP_CONFIG}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Marathon.toml" "${RECOMP_CONFIG}")

set(MARATHON_RECOMP_PPC_RECOMPILED_SOURCES
    "${MARATHON_RECOMP_GENERATED_DIR}/ppc/ppc_config.h"
    "${MARATHON_RECOMP_GENERATED_DIR}/ppc/ppc_context.h"
    "${MARATHON_RECOMP_GENERATED_DIR}/ppc/ppc_func_mapping.cpp"
    "${MARATHON_RECOMP_GENERATED_DIR}/ppc/ppc_recomp_shared.h"
)

foreach(i RANGE 0 145)
    list(APPEND MARATHON_RECOMP_PPC_RECOMPILED_SOURCES "${MARATHON_RECOMP_GENERATED_DIR}/ppc/ppc_recomp.${i}.cpp")
endforeach()

add_custom_command(
    OUTPUT 
        ${MARATHON_RECOMP_PPC_RECOMPILED_SOURCES}
    COMMAND 
        $<TARGET_FILE:XenonRecomp> "${CMAKE_CURRENT_BINARY_DIR}/Marathon.toml" "${MARATHON_RECOMP_TOOLS_ROOT}/XenonRecomp/XenonUtils/ppc_context.h"
    DEPENDS 
        "${MARATHON_RECOMP_XEX_INPUT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/Marathon.toml"
        "${CMAKE_CURRENT_BINARY_DIR}/Marathon.toml"
    USES_TERMINAL
    VERBATIM
)

set(EXTRACTED_SHADER_DIR "${MARATHON_RECOMP_GENERATED_DIR}/shader/input")
set(EXTRACTED_SHADER_STAMP "${MARATHON_RECOMP_GENERATED_DIR}/shader/extracted.stamp")
add_custom_command(
    OUTPUT
        "${EXTRACTED_SHADER_STAMP}"
    COMMAND
        ${CMAKE_COMMAND} -E rm -rf "${EXTRACTED_SHADER_DIR}"
    COMMAND
        $<TARGET_FILE:u8extract> "${MARATHON_RECOMP_shader_INPUT}" "${EXTRACTED_SHADER_DIR}"
    COMMAND
        $<TARGET_FILE:u8extract> "${MARATHON_RECOMP_shader_lt_INPUT}" "${EXTRACTED_SHADER_DIR}"
    COMMAND
        ${CMAKE_COMMAND} -E touch "${EXTRACTED_SHADER_STAMP}"
    DEPENDS
        "${MARATHON_RECOMP_shader_INPUT}"
        "${MARATHON_RECOMP_shader_lt_INPUT}"
    VERBATIM
)

add_custom_target(marathon_extract_shaders DEPENDS "${EXTRACTED_SHADER_STAMP}")

set(XENOS_RECOMP_ROOT "${MARATHON_RECOMP_TOOLS_ROOT}/XenosRecomp/XenosRecomp")
set(XENOS_RECOMP_INCLUDE "${XENOS_RECOMP_ROOT}/shader_common.h")

if(NOT MARATHON_RECOMP_IOS)
target_compile_definitions(XenosRecomp PRIVATE 
    XENOS_RECOMP_INPUT=\"${EXTRACTED_SHADER_DIR}\"
    XENOS_RECOMP_OUTPUT=\"${MARATHON_RECOMP_GENERATED_DIR}/shader/shader_cache.cpp\"
    XENOS_RECOMP_INCLUDE_INPUT=\"${XENOS_RECOMP_INCLUDE}\"
    MARATHON_RECOMP
    XENOS_RECOMP_METAL_SDK=\"${MARATHON_RECOMP_METAL_SDK}\"
)
if(MARATHON_RECOMP_METAL_SDK STREQUAL "iphoneos")
    target_compile_definitions(XenosRecomp PRIVATE XENOS_RECOMP_METAL_IOS)
endif()
endif()

file(GLOB XENOS_RECOMP_SOURCES 
    "${XENOS_RECOMP_ROOT}/*.h"
    "${XENOS_RECOMP_ROOT}/*.cpp"
)

add_custom_command(
    OUTPUT 
        "${MARATHON_RECOMP_GENERATED_DIR}/shader/shader_cache.cpp"
    COMMAND
        $<TARGET_FILE:XenosRecomp>
    DEPENDS 
        "${EXTRACTED_SHADER_STAMP}"
        ${XENOS_RECOMP_SOURCES}
        ${XENOS_RECOMP_INCLUDE}
    USES_TERMINAL
    VERBATIM
)

add_custom_target(marathon_codegen DEPENDS
    ${MARATHON_RECOMP_PPC_RECOMPILED_SOURCES}
    "${MARATHON_RECOMP_GENERATED_DIR}/shader/shader_cache.cpp")
add_dependencies(marathon_codegen marathon_extract_shaders)
if(MARATHON_RECOMP_HOST_TOOLS_ONLY)
    return()
endif()

add_library(MarathonRecompLib
    ${MARATHON_RECOMP_PPC_RECOMPILED_SOURCES}
    "shader/shader_cache.h"
    "${MARATHON_RECOMP_GENERATED_DIR}/shader/shader_cache.cpp"
)

# Xcode requires one ordered owner for custom commands shared by targets.
add_dependencies(MarathonRecompLib marathon_codegen)

target_include_directories(MarathonRecompLib PUBLIC "${MARATHON_RECOMP_GENERATED_DIR}" ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(MarathonRecompLib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/shader")
target_include_directories(MarathonRecompLib PRIVATE "${MARATHON_RECOMP_TOOLS_ROOT}/XenonRecomp/thirdparty/simde")
target_precompile_headers(MarathonRecompLib PUBLIC "${MARATHON_RECOMP_GENERATED_DIR}/ppc/ppc_recomp_shared.h")
