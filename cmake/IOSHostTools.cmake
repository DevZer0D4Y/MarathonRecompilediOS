# Only native executables may run during an iOS cross build. Build these first
# with the ios-host-tools preset; never link the host DXC dylib into the app.
foreach(tool file_to_c u8extract XenonRecomp XenosRecomp)
    if(NOT EXISTS "${MARATHON_RECOMP_HOST_TOOLS_DIR}/${tool}")
        message(FATAL_ERROR "Missing native tool ${tool}. Run ios/build.sh on a Mac first.")
    endif()
    add_executable(${tool} IMPORTED GLOBAL)
    set_target_properties(${tool} PROPERTIES IMPORTED_LOCATION "${MARATHON_RECOMP_HOST_TOOLS_DIR}/${tool}")
endforeach()

set(THIRDPARTY_ROOT "${CMAKE_SOURCE_DIR}/tools/XenonRecomp/thirdparty")
add_subdirectory("${THIRDPARTY_ROOT}" "xenon-thirdparty")
add_subdirectory("${CMAKE_SOURCE_DIR}/tools/XenonRecomp/XenonUtils" "xenon-utils")
add_subdirectory("${CMAKE_SOURCE_DIR}/tools/XenosRecomp/thirdparty/zstd/build/cmake" "zstd")

set(DXC_HOST_ROOT "${CMAKE_SOURCE_DIR}/tools/XenosRecomp/thirdparty/dxc-bin")
if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(DXC_HOST_ARCH arm64)
else()
    set(DXC_HOST_ARCH x64)
endif()
# Cached like dxc-bin's own definition, so MarathonRecomp/CMakeLists.txt sees it outside this directory's scope.
set(DIRECTX_DXC_TOOL "DYLD_LIBRARY_PATH=${DXC_HOST_ROOT}/lib/${DXC_HOST_ARCH}" "${DXC_HOST_ROOT}/bin/${DXC_HOST_ARCH}/dxc-macos" CACHE INTERNAL "")
