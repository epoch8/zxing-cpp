cmake_minimum_required(VERSION 3.15)
project(ZXingPython)

set (pybind11_git_repo https://github.com/pybind/pybind11.git)
set (pybind11_git_rev v2.10.4)

# check if we are called from the top-level ZXing project
get_directory_property(hasParent PARENT_DIRECTORY)
if (NOT hasParent)
    # Build with C++20 by default (which enables position independent DataMatrix detection).
    set(CMAKE_CXX_STANDARD 17)
    # Allow the fallback to earlier versions of the compiler does not support it.
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    option (BUILD_SHARED_LIBS "Link python module to shared lib" OFF)
    option (BUILD_WRITERS "Build with writer support (encoders)" ON)
    option (BUILD_READERS "Build with reader support (decoders)" ON)
    set(BUILD_DEPENDENCIES "AUTO")
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/core)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/core ZXing EXCLUDE_FROM_ALL)
        include(${CMAKE_CURRENT_SOURCE_DIR}/zxing.cmake)
    else()
        message(FATAL_ERROR "Unable to locate zxing source code")
    endif()
endif()

# Use MediaPipe's OpenCV approach (like Android build)  
# Don't use find_package - use direct paths and linking like Android
set(OPENCV_INCLUDE_PATH "/usr/local/include")
set(OPENCV_LIBS_PATH "/usr/local/lib")

# Use conda environment's runtime library path to resolve libstdc++ conflicts
if(DEFINED ENV{CONDA_PREFIX})
    set(CONDA_LIB_PATH "$ENV{CONDA_PREFIX}/lib")
    message(STATUS "Using conda lib path: ${CONDA_LIB_PATH}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${OPENCV_INCLUDE_PATH}")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath,${CONDA_LIB_PATH}")
else()
    # Fallback to system include path (Android style)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${OPENCV_INCLUDE_PATH}")
endif()

zxing_add_package(pybind11 pybind11 ${pybind11_git_repo} ${pybind11_git_rev})

# build the python module - unwrap as separate object to avoid core ZXing OpenCV dependency
add_library(unwrap_helper STATIC unwarp_preprocess.cpp delaunator.cc)
target_include_directories(unwrap_helper PRIVATE ${OPENCV_INCLUDE_PATH})
# Add -fPIC for shared library compatibility
set_target_properties(unwrap_helper PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(unwrap_helper PRIVATE 
    ${OPENCV_LIBS_PATH}/libopencv_core.so
    ${OPENCV_LIBS_PATH}/libopencv_imgproc.so  
    ${OPENCV_LIBS_PATH}/libopencv_imgcodecs.so
)

pybind11_add_module(zxingcpp zxing.cpp)

# Define macros to enable reader and writer functions  
target_compile_definitions(zxingcpp PRIVATE ZXING_BUILD_READERS ZXING_BUILD_WRITERS PYBIND11_DETAILED_ERROR_MESSAGES)

# Link ZXing (without OpenCV) and our OpenCV-enabled unwrap helper separately
target_link_libraries(zxingcpp PRIVATE ZXing::ZXing unwrap_helper)

if (BUILD_READERS AND BUILD_WRITERS)
    add_test(NAME PythonTest COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test.py -v)
    set_property(TEST PythonTest PROPERTY ENVIRONMENT PYTHONPATH=$<TARGET_FILE_DIR:zxingcpp>)
endif()

install(TARGETS zxingcpp
        COMPONENT python
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
