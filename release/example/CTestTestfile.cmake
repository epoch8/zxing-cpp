# CMake generated Testfile for 
# Source directory: /home/pixml/git_parallel/forked_mediapipe/zxing-cpp/example
# Build directory: /home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/example
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ZXingWriterTest "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/example/ZXingWriter" "qrcode" "I have the best words." "test.png")
set_tests_properties(ZXingWriterTest PROPERTIES  _BACKTRACE_TRIPLES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/example/CMakeLists.txt;10;add_test;/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/example/CMakeLists.txt;0;")
add_test(ZXingReaderTest "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/release/example/ZXingReader" "-fast" "-format" "qrcode" "test.png")
set_tests_properties(ZXingReaderTest PROPERTIES  _BACKTRACE_TRIPLES "/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/example/CMakeLists.txt;20;add_test;/home/pixml/git_parallel/forked_mediapipe/zxing-cpp/example/CMakeLists.txt;0;")
