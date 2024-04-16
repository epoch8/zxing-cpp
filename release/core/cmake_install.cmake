# Install script for directory: /home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/home/pixml/Android/Sdk/ndk-bundle/android-ndk-r26/android-ndk-r26/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libZXing.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libZXing.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libZXing.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/libZXing.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libZXing.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libZXing.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/home/pixml/Android/Sdk/ndk-bundle/android-ndk-r26/android-ndk-r26/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libZXing.so")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ZXing" TYPE FILE FILES
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/BarcodeFormat.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/BitHacks.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/ByteArray.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/CharacterSet.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Flags.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/GTIN.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/TextUtfEncoding.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/ZXAlgorithms.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/ZXConfig.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Content.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/DecodeHints.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Error.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/ImageView.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Point.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Quadrilateral.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/ReadBarcode.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Result.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/StructuredAppend.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/BitMatrix.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/BitMatrixIO.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Matrix.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/MultiFormatWriter.h"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/core/src/Range.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/ZXing" TYPE FILE FILES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/ZXVersion.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets.cmake"
         "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/CMakeFiles/Export/f9e04a807b27a41299a115186893fdf1/ZXingTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing/ZXingTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing" TYPE FILE FILES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/CMakeFiles/Export/f9e04a807b27a41299a115186893fdf1/ZXingTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing" TYPE FILE FILES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/CMakeFiles/Export/f9e04a807b27a41299a115186893fdf1/ZXingTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/zxing.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/ZXing" TYPE FILE FILES
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/ZXingConfig.cmake"
    "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/core/ZXingConfigVersion.cmake"
    )
endif()

