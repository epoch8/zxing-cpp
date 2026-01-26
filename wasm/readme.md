## Подготовка

1. Установка emscripten
	- По установке emscripten есть инструкция у них на сайте https://emscripten.org/docs/getting_started/downloads.html
2. Сборка OpenCV
	- По сборке OpenCV под wasm так же есть инструкция https://docs.opencv.org/4.x/d4/da1/tutorial_js_setup.html
	- Я скачивал последний тег https://github.com/opencv/opencv/releases/tag/4.13.0
	- Собирал я такими командами для обычной версии и для simd
		- `emcmake python ~/opencv/platforms/js/build_js.py build_js --disable_single_file --cmake_option="-DCMAKE_CXX_STANDARD=17"`
		- `emcmake python ~/opencv/platforms/js/build_js.py build_js --disable_single_file --simd --cmake_option="-DCMAKE_CXX_STANDARD=17"`
	- С при сборке simd версии вылетала ошибка компиляции DNN модуля, я решил это тем, что в build_js.py на [этой](https://github.com/opencv/opencv/blob/a9f06448c8a848dc26977c5a5b94e7836ac5c2ab/platforms/js/build_js.py#L126) строке выключил DNN `-DBUILD_opencv_dnn=OFF`

## Сборка

Сборка самого ZXing выполняется скриптом `build_wasm.sh`, в нём надо указать
- `CMAKE_TOOLCHAIN_FILE` из поставленного emscripten
- `OPENCV_PATH` из сбилженного opencv, simd или обычной версии в зависимости от сборки