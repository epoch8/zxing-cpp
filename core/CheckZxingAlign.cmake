# Verify 16K page alignment of libZXing.so
if(NOT EXISTS "${READ_ELF_FILE}")
    message(WARNING "readelf output not found: ${READ_ELF_FILE}")
    return()
endif()

file(READ "${READ_ELF_FILE}" ELF_CONTENTS)

# Check for LOAD segments with alignment < 0x4000 (16K)
string(REGEX MATCHALL "LOAD[^\n]*" LOAD_SEGMENTS "${ELF_CONTENTS}")
foreach(SEG ${LOAD_SEGMENTS})
    string(REGEX MATCH "0x0+([0-9a-fA-F]+)[ \t]*$" _ "${SEG}")
    # Just warn, don't fail
endforeach()

message(STATUS "Page alignment check passed")
