#skip includes files in cp

sudo rm -r  v8/
sudo rm -r  v7/
sudo rm -r  x86/
sudo rm -r  x86_64/
sudo rm -r  release/

mkdir v8/
mkdir v7/
mkdir x86/
mkdir x86_64/
mkdir release/

cd release/

cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/pixml/Android/Sdk/ndk-bundle/android-ndk-r26/android-ndk-r26/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="-llog"
make -j 16
sudo make install

sudo cp /usr/local/lib/libZXing.so ../v8/

sudo rm -r *

cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/pixml/Android/Sdk/ndk-bundle/android-ndk-r26/android-ndk-r26/build/cmake/android.toolchain.cmake -DANDROID_ABI=armeabi-v7a -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="-llog"
make -j 16
sudo make install

sudo cp /usr/local/lib/libZXing.so ../v7/

cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/pixml/Android/Sdk/ndk-bundle/android-ndk-r26/android-ndk-r26/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86 -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="-llog"
make -j 16
sudo make install

sudo cp /usr/local/lib/libZXing.so ../x86/

sudo rm -r *

cmake .. -DCMAKE_TOOLCHAIN_FILE=/home/pixml/Android/Sdk/ndk-bundle/android-ndk-r26/android-ndk-r26/build/cmake/android.toolchain.cmake -DANDROID_ABI=x86_64 -DANDROID_NATIVE_API_LEVEL=21 -DCMAKE_SHARED_LINKER_FLAGS="-llog"
make -j 16
sudo make install

sudo cp /usr/local/lib/libZXing.so ../x86_64/
