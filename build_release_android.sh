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

# Resolve OpenCV Android SDK paths (prefer MediaPipe's Bazel android_opencv, fall back to local SDK)
# You can override by exporting OPENCV_ANDROID_SDK to point at .../OpenCV-android-sdk/sdk/native or Bazel's .../external/android_opencv/sdk/native
if [ -n "${OPENCV_ANDROID_SDK}" ]; then
  OPENCV_SDK_NATIVE_ROOT="${OPENCV_ANDROID_SDK}"
else
  BAZEL_BASE="$(bazel info output_base 2>/dev/null || true)"
  if [ -n "${BAZEL_BASE}" ] && [ -d "${BAZEL_BASE}/external/android_opencv/sdk/native" ]; then
    OPENCV_SDK_NATIVE_ROOT="${BAZEL_BASE}/external/android_opencv/sdk/native"
  else
    OPENCV_SDK_NATIVE_ROOT="/home/lev/StudioProjects/mediapipe/OpenCV-android-sdk/sdk/native"
  fi
fi

OPENCV_JNI_PATH="${OPENCV_SDK_NATIVE_ROOT}/jni"
OPENCV_INCLUDE_PATH="${OPENCV_SDK_NATIVE_ROOT}/jni/include"
OPENCV_LIBS_PATH="${OPENCV_SDK_NATIVE_ROOT}/libs"

# Basic validation to fail fast with a clear message
if [ ! -f "${OPENCV_JNI_PATH}/OpenCVConfig.cmake" ] && [ ! -f "${OPENCV_JNI_PATH}/abi-arm64-v8a/OpenCVConfig.cmake" ]; then
  echo "❌ OpenCVConfig.cmake not found under ${OPENCV_JNI_PATH}.\nSet OPENCV_ANDROID_SDK to your OpenCV-android-sdk/sdk/native or ensure Bazel's android_opencv is available."
  exit 1
fi

# Add C++17 support, OpenCV include path, and fix NEON macro issue
CMAKE_CXX_FLAGS="-std=c++17 -I${OPENCV_INCLUDE_PATH} -DCV_CPU_HAS_SUPPORT_NEON=0 -DCV_CPU_HAS_SUPPORT_SSE2=0"

# Function to get architecture-specific linking flags
get_linker_flags() {
    local arch=$1
    echo "-llog -Wl,-z,common-page-size=4096 -Wl,-z,max-page-size=65536 -L${OPENCV_LIBS_PATH}/${arch} -lopencv_java4"
}

# Build for arm64-v8a
echo "Building for arm64-v8a..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "arm64-v8a")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=arm64-v8a -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON -DANDROID_STL=c++_shared

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for arm64-v8a"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../arm64-v8a/
    # cp core/libc++_shared.so ../arm64-v8a/
    file ../arm64-v8a/libZXing.so
    echo "✅ arm64-v8a build successful"
else
    echo "❌ arm64-v8a build failed - libZXing.so not found"
fi

# Build for armeabi-v7a
echo "Building for armeabi-v7a..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "armeabi-v7a")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=armeabi-v7a -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON -DANDROID_STL=c++_shared

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for armeabi-v7a"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../armeabi-v7a/
    # cp core/libc++_shared.so ../armeabi-v7a/
    file ../armeabi-v7a/libZXing.so
    echo "✅ armeabi-v7a build successful"
else
    echo "❌ armeabi-v7a build failed - libZXing.so not found"
fi

# Build for x86
echo "Building for x86..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "x86")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=x86 -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON -DANDROID_STL=c++_shared

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for x86"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../x86/
    # cp core/libc++_shared.so ../x86/
    file ../x86/libZXing.so
    echo "✅ x86 build successful"
else
    echo "❌ x86 build failed - libZXing.so not found"
fi

# Build for x86_64
echo "Building for x86_64..."
rm -rf *  # Clean the release directory
ARCH_LINKER_FLAGS=$(get_linker_flags "x86_64")
cmake -B . -S .. -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_JNI_PATH" -DCMAKE_CXX_FLAGS="$CMAKE_CXX_FLAGS" -DANDROID_ABI=x86_64 -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="$ARCH_LINKER_FLAGS" -DBUILD_FOR_AARM=ON -DANDROID_STL=c++_shared

if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for x86_64"
    exit 1
fi

make -j 16

if [ -f "core/libZXing.so" ]; then
    cp core/libZXing.so ../x86_64/
    # cp core/libc++_shared.so ../x86_64/
    file ../x86_64/libZXing.so
    echo "✅ x86_64 build successful"
else
    echo "❌ x86_64 build failed - libZXing.so not found"
fi

echo "Build complete! Libraries are in the respective architecture directories."
