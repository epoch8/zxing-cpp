echo ========= Remove previous builds
rm -rf _builds
rm -rf ZXing.xcframework

echo ========= Create project structure
cmake -S../../ -B_builds -GXcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_INSTALL_PREFIX=`pwd`/_install \
    -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DBUILD_UNIT_TESTS=NO \
    -DBUILD_BLACKBOX_TESTS=NO \
    -DBUILD_EXAMPLES=NO \
    -DBUILD_APPLE_FRAMEWORK=YES \
    -DCMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES=YES \
    -DCMAKE_VERBOSE_MAKEFILE=ON

echo ========= Build the sdk for simulators
xcodebuild -project _builds/ZXing.xcodeproj build \
    -target ZXing \
    -parallelizeTargets \
    -configuration Release \
    -hideShellScriptEnvironment \
    -sdk iphonesimulator -arch x86_64 -arch arm64 \
    2>&1 | tee build-simulator.log

echo ========= Build the sdk for iOS
xcodebuild -project _builds/ZXing.xcodeproj build \
    -target ZXing \
    -parallelizeTargets \
    -configuration Release \
    -hideShellScriptEnvironment \
    -sdk iphoneos \
    2>&1 | tee build-ios.log

echo ========= Create the xcframework
xcodebuild -create-xcframework \
    -framework ./_builds/core/Release-iphonesimulator/ZXing.framework \
    -framework ./_builds/core/Release-iphoneos/ZXing.framework \
    -output ZXing.xcframework
