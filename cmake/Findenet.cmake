######################################################
# Once done this will define
#  ENET_FOUND - System has libfabric
#  ENET_INCLUDE_DIRS - The libfabric include directories
#  ENET_LIBRARIES - The libraries needed to use libfabric
#  ENET_DEFINITIONS - Compiler switches required for using libfabric

######################################################
set(ENET_ENET "" CACHE STRING "Help cmake to find enet library on your system.")

######################################################
find_path(ENET_INCLUDE_DIR enet/enet.h
    HINTS ${ENET_DIR}/include)

######################################################
find_library(ENET_LIBRARY NAMES enet
    HINTS ${ENET_DIR}/lib)

######################################################
set(ENET_LIBRARIES ${ENET_LIBRARY} )
set(ENET_INCLUDE_DIRS ${ENET_INCLUDE_DIR} )

######################################################
include(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set ENET_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(enet  DEFAULT_MSG
    ENET_LIBRARY ENET_INCLUDE_DIR)

######################################################
mark_as_advanced(ENET_INCLUDE_DIR ENET_LIBRARY )
