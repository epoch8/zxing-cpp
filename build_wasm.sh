CMAKE_TOOLCHAIN_FILE="/home/chorbier/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
OPENCV_PATH="/home/chorbier/opencv_wasm/build_js_simd"
ENABLE_WASM_SIMD="ON"
BUILD_TYPE="Release"

mkdir -p build_wasm

cd build_wasm

pyenv local 3.13


# Build for WASM
echo "Building for WASM"
emcmake cmake -B . -S .. -DENABLE_WASM_SIMD="$ENABLE_WASM_SIMD" -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN_FILE" -DOpenCV_DIR="$OPENCV_PATH" -DBUILD_FOR_WASM=ON -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
if [ ! -f "Makefile" ]; then
    echo "❌ CMake failed to generate Makefile for WASM"
    exit 1
fi

make -j 8

echo "Build complete! Libraries are in the respective architecture directories."
