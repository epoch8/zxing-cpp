#skip includes files in cp

# Parse named arguments
for arg in "$@"; do
  case $arg in
    CMAKE_TOOLCHAIN_FILE=*)
      CMAKE_TOOLCHAIN_FILE="${arg#*=}"
      ;;
  esac
done

# Get the absolute path to the zxing-cpp directory
ZXING_CPP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Navigate to the zxing-cpp directory
cd "$ZXING_CPP_DIR"

sudo rm -rf zxing-cpp.release
sudo rm -rf arm64-v8a/
sudo rm -rf armeabi-v7a/
sudo rm -rf x86/
sudo rm -rf x86_64/
sudo rm -rf release/

mkdir -p arm64-v8a/
mkdir -p armeabi-v7a/
mkdir -p x86/
mkdir -p x86_64/
mkdir -p release/

cd release/

# Use Bazel's OpenCV 4.x Android SDK (populated after a successful Bazel build)
BAZEL_CACHE="/home/lev/.cache/bazel/_bazel_lev/5ee74aee6f1c35e49eadf238c6df3c19"
OPENCV_JNI_PATH="${BAZEL_CACHE}/external/android_opencv/sdk/native/jni"
OPENCV_INCLUDE_PATH="${BAZEL_CACHE}/external/android_opencv/sdk/native/jni/include"
OPENCV_LIBS_PATH="${BAZEL_CACHE}/external/android_opencv/sdk/native/libs"

# Add C++20 support, OpenCV include path, and fix NEON macro issue
CMAKE_CXX_FLAGS="-std=c++20 -I${OPENCV_INCLUDE_PATH} -DCV_CPU_HAS_SUPPORT_NEON=0 -DCV_CPU_HAS_SUPPORT_SSE2=0"

# Function to get architecture-specific linking flags
get_linker_flags() {
    local arch=$1
    echo "-llog -Wl,-z,common-page-size=4096 -Wl,-z,max-page-size=65536 -L${OPENCV_LIBS_PATH}/${arch} -lopencv_java4"
}

# Build for arm64-v8a
echo "Building for arm64-v8a..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "arm64-v8a")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=arm64-v8a -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for arm64-v8a"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../arm64-v8a/
    file ../arm64-v8a/libZXing.so
    echo "✅ arm64-v8a build successful"
else
    echo "❌ arm64-v8a build failed - libZXing.so not found"
fi

# Build for armeabi-v7a
echo "Building for armeabi-v7a..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "armeabi-v7a")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=armeabi-v7a -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for armeabi-v7a"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../armeabi-v7a/
    file ../armeabi-v7a/libZXing.so
    echo "✅ armeabi-v7a build successful"
else
    echo "❌ armeabi-v7a build failed - libZXing.so not found"
fi

# Build for x86
echo "Building for x86..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "x86")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=x86 -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for x86"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../x86/
    file ../x86/libZXing.so
    echo "✅ x86 build successful"
else
    echo "❌ x86 build failed - libZXing.so not found"
fi

# Build for x86_64
echo "Building for x86_64..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "x86_64")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=x86_64 -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for x86_64"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../x86_64/
    file ../x86_64/libZXing.so
    echo "✅ x86_64 build successful"
else
    echo "❌ x86_64 build failed - libZXing.so not found"
fi

echo "Build complete! Libraries are in the respective architecture directories."
