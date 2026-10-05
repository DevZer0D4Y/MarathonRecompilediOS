set(MARATHON_RECOMP_IOS_BUNDLE_ID "com.devz.sonic2006" CACHE STRING "iOS bundle identifier")
set(MARATHON_RECOMP_IOS_DEVELOPMENT_TEAM "" CACHE STRING "Apple development team ID used to sign the iOS app")
# Free Apple accounts can't sign these entitlements. Without them, the game still runs on devices
# with 6 GB of RAM or more, but devices with less may close it at launch.
option(MARATHON_RECOMP_IOS_EXTENDED_MEMORY "Request the extended virtual addressing and increased memory limit entitlements" ON)

target_compile_definitions(MarathonRecomp PRIVATE MARATHON_RECOMP_IOS)
# Matches MarathonRecompLib, whose weak guest functions the app's hooks replace (see its CMakeLists.txt).
target_compile_options(MarathonRecomp PRIVATE -fvisibility=hidden)
target_compile_options(MarathonRecomp PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(MarathonRecomp PRIVATE SDL2::SDL2main
    "-framework UIKit" "-framework Foundation" "-framework Metal"
    "-framework CoreGraphics" "-framework QuartzCore" "-framework GameController"
    "-framework UniformTypeIdentifiers" "-framework AVFoundation")

set(MARATHON_RECOMP_IOS_ASSET_CATALOG "${CMAKE_CURRENT_SOURCE_DIR}/res/ios/Assets.xcassets")
set_source_files_properties("${MARATHON_RECOMP_IOS_ASSET_CATALOG}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(MarathonRecomp PRIVATE "${MARATHON_RECOMP_IOS_ASSET_CATALOG}")

set_target_properties(MarathonRecomp PROPERTIES
    OUTPUT_NAME MarathonRecomp
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/res/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${MARATHON_RECOMP_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "Sonic (2006)"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MACOS_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${MACOS_BUNDLE_VERSION}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${MARATHON_RECOMP_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)

if(MARATHON_RECOMP_IOS_EXTENDED_MEMORY)
    set_target_properties(MarathonRecomp PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/ios/MarathonRecomp.entitlements")
endif()

if(MARATHON_RECOMP_IOS_DEVELOPMENT_TEAM)
    set_target_properties(MarathonRecomp PROPERTIES
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${MARATHON_RECOMP_IOS_DEVELOPMENT_TEAM}")
endif()
