set(MARATHON_RECOMP_IOS_BUNDLE_ID "dev.sonicnext.MarathonRecomp.ios" CACHE STRING "iOS bundle identifier")
target_compile_definitions(MarathonRecomp PRIVATE MARATHON_RECOMP_IOS)
target_compile_options(MarathonRecomp PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(MarathonRecomp PRIVATE SDL2::SDL2main
    "-framework UIKit" "-framework Foundation" "-framework Metal"
    "-framework CoreGraphics" "-framework QuartzCore" "-framework GameController"
    "-framework UniformTypeIdentifiers" "-framework AVFoundation")
set_target_properties(MarathonRecomp PROPERTIES
    OUTPUT_NAME MarathonRecomp
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/res/ios/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${MARATHON_RECOMP_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "Marathon Recompiled"
    MACOSX_BUNDLE_BUNDLE_VERSION "${MACOS_BUNDLE_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${MACOS_BUNDLE_VERSION}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_SOURCE_DIR}/ios/MarathonRecomp.entitlements"
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST NO
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
