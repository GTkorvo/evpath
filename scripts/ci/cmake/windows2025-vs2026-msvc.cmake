# Client maintainer: eisen@cc.gatech.edu

# windows-2025 runner image ships Visual Studio 2026; needs CMake >= 4.2
# Library and transport warnings are errors, as in ADIOS2's Windows CI
string(APPEND dashboard_cache "EVPATH_WARNINGS_AS_ERRORS:BOOL=ON
")
set(CTEST_CMAKE_GENERATOR "Visual Studio 18 2026")
set(CTEST_CMAKE_GENERATOR_PLATFORM x64)

list(APPEND CTEST_UPDATE_NOTES_FILES "${CMAKE_CURRENT_LIST_FILE}")
include(${CMAKE_CURRENT_LIST_DIR}/windows-common.cmake)
