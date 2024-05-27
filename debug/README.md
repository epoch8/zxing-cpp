## Build Instructions
Чтобы потестить что-то внутри ZXing, лезем в исходники, меняем там код, который мы хотим потестить и собираем ZXing:
```
cd ../
cmake -S zxing-cpp -B zxing-cpp.release -DCMAKE_BUILD_TYPE=Release
cmake --build zxing-cpp.release -j8
```
SOшник, который получается после билда и предназначен для Linux падает в zxing-cpp.release/core.

После того как собрали ZXing, делаем:
```
cd zxing-cpp/debug
sudo cmake . 
sudo make
```
В zxing-cpp/debug падает BarcodeReaderTest, который мы можем запустить из командной строки 
```
./BarcodeReaderTest
```
