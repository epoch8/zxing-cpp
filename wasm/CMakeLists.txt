zxing_add_package_stb()

find_package(OpenCV REQUIRED PATHS ${OpenCV_DIR} NO_DEFAULT_PATH)
if (OpenCV_FOUND)
    set(DEBUG_SOURCES
        WasmReadCode.cpp
        libs/UnwarpPreprocess.cpp
    )
    add_executable (WasmReadCode ${DEBUG_SOURCES})
    target_include_directories (WasmReadCode PRIVATE libs)
    target_include_directories (WasmReadCode PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries (WasmReadCode ZXing::ZXing ${OpenCV_LIBS})
    target_compile_options(WasmReadCode PRIVATE 
        # -gsource-map=inline
        -fdebug-compilation-dir=..
    )

    set_target_properties(WasmReadCode PROPERTIES
        LINK_FLAGS "\
            -s WASM=1 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s MODULARIZE=1 \
            -s EXPORT_ES6=1 \
            -s EXPORT_NAME='createZXingModule' \
            -s EXPORTED_RUNTIME_METHODS='[\"ccall\", \"cwrap\"]' \
            -fexceptions \
            -msimd128 \
            -lembind \
            -g3 \
        "
    )
    # -gsource-map=inline
            # -gsource-map=inline \
            # --source-map-base / \

    # --source-map-base / \
            # -gsource-map=inline \
        
    # -gline-tables-only \


    # -gsource-map=inline \
            # -gseparate-dwarf \
            # -O3 \
                # -s SINGLE_FILE=1 \
            # -gsource-map=inline \
            # --source-map-base / \

else ()
    message("INFO: OpenCV not found, skipping DebugReadbardcode")
endif()