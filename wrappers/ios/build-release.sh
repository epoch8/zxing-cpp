#!/bin/bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
OUTPUT=${1:-"$SCRIPT_DIR/ZXing.xcframework"}
OPENCV=${OPENCV_IOS_XCFRAMEWORK:-"$SCRIPT_DIR/vendor/opencv2.xcframework"}
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/zxing-ios.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
export PATH="/Applications/CMake.app/Contents/bin:$PATH"

build_framework() {
    local sdk=$1 archs=$2 build_dir="$STAGE/$1"
    cmake -S "$REPO_ROOT" -B "$build_dir" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="$(xcrun --sdk "$sdk" --show-sdk-path)" \
        -DCMAKE_OSX_ARCHITECTURES="$archs" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
        -DCMAKE_CXX_STANDARD=20 -DCMAKE_OBJCXX_STANDARD=20 \
        -DBUILD_SHARED_LIBS=ON -DBUILD_APPLE_FRAMEWORK=ON \
        -DBUILD_READERS=ON -DBUILD_WRITERS=ON \
        -DBUILD_UNIT_TESTS=OFF -DBUILD_BLACKBOX_TESTS=OFF -DBUILD_EXAMPLES=OFF \
        -DOPENCV_IOS_XCFRAMEWORK="$OPENCV"
    cmake --build "$build_dir" --target ZXing --parallel "${JOBS:-4}"
    local framework="$build_dir/core/ZXing.framework"
    mkdir -p "$framework/Modules"
    cp "$SCRIPT_DIR/Sources/Wrapper/framework.modulemap" "$framework/Modules/module.modulemap"
}

build_framework iphonesimulator 'arm64;x86_64'
build_framework iphoneos arm64
xcodebuild -create-xcframework \
    -framework "$STAGE/iphonesimulator/core/ZXing.framework" \
    -framework "$STAGE/iphoneos/core/ZXing.framework" \
    -output "$STAGE/ZXing.xcframework"

if [ -d "$OUTPUT" ]; then
    rsync -a --delete "$STAGE/ZXing.xcframework/" "$OUTPUT/"
else
    mkdir -p "$(dirname "$OUTPUT")"
    mv "$STAGE/ZXing.xcframework" "$OUTPUT"
fi
