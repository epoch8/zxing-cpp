cmake_minimum_required(VERSION 3.15)
project(BarcodeReaderTest)

option (BUILD_WRITERS "Build with writer support (encoders)" ON)
option (BUILD_READERS "Build with reader support (decoders)" ON)
option (BUILD_EXAMPLES "Build the example barcode reader/writer applications" ON)
option (BUILD_BLACKBOX_TESTS "Build the black box reader/writer tests" OFF)
option (BUILD_UNIT_TESTS "Build the unit tests (don't enable for production builds)" OFF)
option (BUILD_PYTHON_MODULE "Build the python module" OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(../core/src/)

find_library(ZXING_LIBRARY NAMES ZXing PATHS /home/lev/StudioProjects/zxing-cpp.release/core NO_DEFAULT_PATH)

message(STATUS "ZXing Library: ${ZXING_LIBRARY}")
include_directories(${OpenCV_INCLUDE_DIRS})
set (OpenCV_DIR /usr/lib/x86_64-linux-gnu/cmake/opencv4)
find_package(OpenCV REQUIRED)

get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
    message(STATUS "dir='${dir}'")
endforeach()
set(SOURCE_FILES DebugReadbardcode.cpp)

add_executable(BarcodeReaderTest DebugReadbardcode.cpp)

target_link_libraries(BarcodeReaderTest ${ZXING_LIBRARY} ${OpenCV_LIBS})

