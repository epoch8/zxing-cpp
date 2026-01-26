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

    option(ENABLE_WASM_SIMD "Enable WebAssembly SIMD128 instructions" OFF)

    if(ENABLE_WASM_SIMD)
        target_compile_options(WasmReadCode PRIVATE -msimd128)
    endif()

    target_link_options(WasmReadCode PRIVATE
        "SHELL:-s WASM=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s MODULARIZE=1"
        "SHELL:-s EXPORT_ES6=1"
        "SHELL:-s EXPORT_NAME='createZXingModule'"
        "SHELL:-lembind"
        "SHELL:-s ENVIRONMENT='web'"
    )

    target_compile_options(WasmReadCode PRIVATE 
        "$<$<CONFIG:Debug>:-g3>"
        "$<$<CONFIG:Debug>:-fexceptions>"
    )
            # -O3 \

            # -fexceptions \
            # -g3 \

else ()
    message("INFO: OpenCV not found, skipping DebugReadbardcode")
endif()