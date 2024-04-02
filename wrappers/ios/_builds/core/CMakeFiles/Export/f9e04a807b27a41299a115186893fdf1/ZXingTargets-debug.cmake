#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "ZXing::ZXing" for configuration "Debug"
set_property(TARGET ZXing::ZXing APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(ZXing::ZXing PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/ZXing.framework/ZXing"
  IMPORTED_SONAME_DEBUG "@rpath/ZXing.framework/ZXing"
  )

list(APPEND _cmake_import_check_targets ZXing::ZXing )
list(APPEND _cmake_import_check_files_for_ZXing::ZXing "${_IMPORT_PREFIX}/lib/ZXing.framework/ZXing" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
