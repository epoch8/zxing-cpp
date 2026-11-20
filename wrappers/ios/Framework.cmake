enable_language(OBJCXX)

set(wrapper "${CMAKE_CURRENT_LIST_DIR}/Sources/Wrapper")
if(CMAKE_OSX_SYSROOT MATCHES "[Ii][Pp]hone[Ss]imulator")
    set(opencv_slice "ios-arm64_x86_64-simulator")
else()
    set(opencv_slice "ios-arm64")
endif()
set(opencv "${OPENCV_IOS_XCFRAMEWORK}/${opencv_slice}/opencv2.framework")
if(NOT EXISTS "${opencv}/opencv2")
    message(FATAL_ERROR "Missing OpenCV iOS framework: ${opencv}")
endif()

set(include_fix "${CMAKE_CURRENT_BINARY_DIR}/ios_include")
file(MAKE_DIRECTORY "${include_fix}")
file(CREATE_LINK "${opencv}/Headers" "${include_fix}/opencv2" SYMBOLIC)
file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/src" "${include_fix}/ZXing" SYMBOLIC)
target_include_directories(ZXing PRIVATE "${include_fix}" "${wrapper}" "${wrapper}/Writer")
target_compile_options(ZXing PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_sources(ZXing PRIVATE
    "${wrapper}/Writer/ZXIBarcodeWriter.mm"
    "${wrapper}/ZXIFormatHelper.mm")

list(APPEND PUBLIC_HEADERS
    src/CrptLadderStats.h
    src/DetectorResult.h
    "${wrapper}/Writer/ZXIBarcodeWriter.h"
    "${wrapper}/ZXIFormat.h"
    "${wrapper}/ZXIErrors.h"
    "${wrapper}/ZXing.h")
target_sources(ZXing PRIVATE ${PUBLIC_HEADERS})
target_link_libraries(ZXing PRIVATE "${opencv}/opencv2" -lz
    "-framework Foundation" "-framework CoreGraphics" "-framework Accelerate")
set_target_properties(ZXing PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
    XCODE_ATTRIBUTE_MODULEMAP_FILE "${wrapper}/framework.modulemap"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
    INSTALL_NAME_DIR "@rpath"
    BUILD_WITH_INSTALL_NAME_DIR YES)
