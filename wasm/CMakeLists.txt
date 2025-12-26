zxing_add_package_stb()

find_package(OpenCV REQUIRED PATHS ${OpenCV_DIR} NO_DEFAULT_PATH)
if (OpenCV_FOUND)
    set(DEBUG_SOURCES
        WasmReadCode.cpp
        # libs/UnwarpPreprocess.cpp
        # libs/delaunator.cpp
    )
    add_executable (WasmReadCode ${DEBUG_SOURCES})
    # target_include_directories (WasmReadCode PRIVATE libs)
    target_include_directories (WasmReadCode PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries (WasmReadCode ZXing::ZXing ${OpenCV_LIBS})
    set_target_properties(WasmReadCode PROPERTIES
        LINK_FLAGS "\
            -s WASM=1 \
            -s MODULARIZE=1 \
            -s EXPORT_ES6=1 \
            -s EXPORT_NAME='createZXingModule' \
            -s SINGLE_FILE=1 \
            -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\"]' \
            -O3 \
            -lembind \
        "
    )
else ()
    message("INFO: OpenCV not found, skipping DebugReadbardcode")
endif()