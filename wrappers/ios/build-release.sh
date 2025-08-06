#!/bin/sh

# Определяем папки для каждой сборки
SIM_BUILD_DIR="_builds_sim"
DEV_BUILD_DIR="_builds_dev"

echo "========= Clean previous builds"
rm -rf ${SIM_BUILD_DIR}
rm -rf ${DEV_BUILD_DIR}
rm -rf ZXing.xcframework

# --- СБОРКА ДЛЯ СИМУЛЯТОРА ---
echo "========= Create project for Simulator in '${SIM_BUILD_DIR}'"
cmake -S../../ -B${SIM_BUILD_DIR} -GXcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_INSTALL_PREFIX=`pwd`/_install \
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DBUILD_UNIT_TESTS=NO \
    -DBUILD_BLACKBOX_TESTS=NO \
    -DBUILD_EXAMPLES=NO \
    -DBUILD_APPLE_FRAMEWORK=YES

echo "========= Build the sdk for Simulators"
xcodebuild -project ${SIM_BUILD_DIR}/ZXing.xcodeproj build \
    -target ZXing \
    -parallelizeTargets \
    -configuration Release \
    -hideShellScriptEnvironment \
    -sdk iphonesimulator -arch x86_64 -arch arm64

# --- СБОРКА ДЛЯ УСТРОЙСТВА ---
echo "========= Create project for Device in '${DEV_BUILD_DIR}'"
cmake -S../../ -B${DEV_BUILD_DIR} -GXcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    "-DCMAKE_OSX_ARCHITECTURES=arm64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_INSTALL_PREFIX=`pwd`/_install \
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DBUILD_UNIT_TESTS=NO \
    -DBUILD_BLACKBOX_TESTS=NO \
    -DBUILD_EXAMPLES=NO \
    -DBUILD_APPLE_FRAMEWORK=YES

echo "========= Build the sdk for iOS"
xcodebuild -project ${DEV_BUILD_DIR}/ZXing.xcodeproj build \
    -target ZXing \
    -parallelizeTargets \
    -configuration Release \
    -hideShellScriptEnvironment \
    -sdk iphoneos

# --- СОЗДАНИЕ XCFRAMEWORK ---
echo "========= Create the xcframework"
xcodebuild -create-xcframework \
    -framework ./${SIM_BUILD_DIR}/core/Release-iphonesimulator/ZXing.framework \
    -framework ./${DEV_BUILD_DIR}/core/Release-iphoneos/ZXing.framework \
    -output ZXing.xcframework

echo "========= DONE! ========= "