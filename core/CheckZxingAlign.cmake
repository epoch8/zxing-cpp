# Usage: -DREAD_ELF_FILE=<path>
if(NOT DEFINED READ_ELF_FILE)
    message(FATAL_ERROR "READ_ELF_FILE not set")
endif()

file(READ "${READ_ELF_FILE}" CONTENTS)

# Make matching easier by ensuring there's always a leading newline
set(CONTENTS "\n${CONTENTS}")

# Grab the first LOAD line (single line)
string(REGEX MATCH "\n[ \t]*LOAD[^\n]*" FIRST_LOAD_LINE "${CONTENTS}")
if(NOT FIRST_LOAD_LINE)
    message(FATAL_ERROR "Could not find a LOAD line in ${READ_ELF_FILE}")
endif()

# Extract the last hex token on the line (this is the alignment column)
string(REGEX MATCH "0x[0-9a-fA-F]+[ \t]*$" ALIGN_HEX "${FIRST_LOAD_LINE}")
if(NOT ALIGN_HEX)
    message(FATAL_ERROR "Could not parse alignment from: ${FIRST_LOAD_LINE}")
endif()

# Normalize
string(STRIP "${ALIGN_HEX}" ALIGN_HEX)

# Accept 16KB (0x4000) or 64KB (0x10000)
if(NOT ALIGN_HEX MATCHES "0x(4000|10000)")
    message(FATAL_ERROR "libZXing.so not 16K/64K aligned; got: ${ALIGN_HEX}")
endif()
