cmake_minimum_required(VERSION 3.0.0)
cmake_policy(VERSION 3.0.0)
project(evpath)

IF( NOT CMAKE_BUILD_TYPE )
SET( CMAKE_BUILD_TYPE "RelWithDebInfo" )
ENDIF()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "dill, atl, ffs")
set(CPACK_RPM_PACKAGE_REQUIRES "dill, atl, ffs")

include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckTypeSize)
include(CheckStructHasMember)
include(CheckCSourceRuns)
include(CreateLibtoolFile)
include(AddLibtoolLibrary)
include(TestBigEndian)
include(CheckBrokenTitanCompiler)
include(CERCS_package)

include(FindCERCSProject)
include(CTest)
configure_file(CTestCustom.ctest.in CTestCustom.ctest @ONLY)

if(MSVC)
  # Force to always compile with W4
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
  endif()
elseif(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
  # Update if necessary
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall ")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall ")
endif()

IF(NOT DEFINED BUILD_SHARED_STATIC)
  SET( BUILD_SHARED_STATIC "BOTH")
ENDIF(NOT DEFINED BUILD_SHARED_STATIC)
IF(NOT DEFINED NO_TRANSPORT_MODULES)
  SET( NO_TRANSPORT_MODULES "FALSE")
ENDIF(NOT DEFINED NO_TRANSPORT_MODULES)

# use, i.e. don't skip the full RPATH for the build tree
SET(CMAKE_SKIP_BUILD_RPATH  FALSE)

# when building, don't use the install RPATH already
# (but later on when installing)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE) 

SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# add the automatically determined parts of the RPATH
# which point to directories outside the build tree to the install RPATH
SET(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)


# the RPATH to be used when installing, but only if it's not a system directory
LIST(FIND CMAKE_PLATFORM_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_PREFIX}/lib" isSystemDir)
IF("${isSystemDir}" STREQUAL "-1")
   SET(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
ENDIF("${isSystemDir}" STREQUAL "-1")

set (CMAKE_MACOSX_RPATH 1)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set (EVPATH_SRC_LIST cm.c cm_control.c cm_formats.c cm_util.c cm_transport.c 
                       cm_lock.c cm_perf.c cm_pbio.c cm_interface.c version.c
                       cm_threadio.c cm_evol.c evp.c response.c metrics.c 
		       dlloader.c ip_config.c chr_time.c
                       revp.c evp_compat.c thin_server.c evp_threads.c ev_dfg.c)

set (CERCS_FIND_OPTIONS)
set (EVPATH_TRANSPORT_DEP_LIBS)

IF (NOT DEFINED CM_SELF_FORMATS)
  SET( CM_SELF_FORMATS 1)
ENDIF (NOT DEFINED CM_SELF_FORMATS)

if (DEFINED BUILD_SHARED_STATIC)   # if build_shared_static is static, default building in transport modules
  string(TOUPPER ${BUILD_SHARED_STATIC} BUILD_SHARED_STATIC) 
  if (${BUILD_SHARED_STATIC} STREQUAL "STATIC")
    if (NOT DEFINED TRANSPORT_MODULES)
      SET (TARGET_CNL 1)
    endif (NOT DEFINED TRANSPORT_MODULES)
  endif (${BUILD_SHARED_STATIC} STREQUAL "STATIC")
endif (DEFINED BUILD_SHARED_STATIC)

IF (DEFINED TARGET_CNL) 
  set (NO_DYNAMIC_LINKING 1)   # dummy dyn linking in cm.c and cm_transport.c
  list (APPEND EVPATH_SRC_LIST cmsockets.c cmudp.c cmselect.c)  # link sockets and select mechanisms in main lib
  list (APPEND CERCS_FIND_OPTIONS "STATIC")
  set (NO_TRANSPORT_MODULES TRUE)
  set (BUILD_SHARED_STATIC STATIC)
ELSE (DEFINED TARGET_CNL)
  find_package(DL)
  IF (NOT "${DL_LIBRARIES}" STREQUAL "")
    list (APPEND EVPATH_TRANSPORT_STATIC_DEPS "-ldl") 
    list (APPEND EVPATH_TRANSPORT_FULL_DEP "-ldl") 
  ENDIF()
ENDIF (DEFINED TARGET_CNL)

if (DEFINED CERCS_USE_INSTALLED) 
   set (CERCS_USE_INSTALLED)
   list (APPEND CERCS_FIND_OPTIONS "USE_INSTALLED")
endif(DEFINED CERCS_USE_INSTALLED) 

FIND_CERCS_PROJECT (atl LIBRARY atl INCLUDES atl.h REQUIRED ${CERCS_FIND_OPTIONS})
FIND_CERCS_PROJECT (ffs LIBRARY ffs INCLUDES ffs.h REQUIRED ${CERCS_FIND_OPTIONS})
FIND_CERCS_PROJECT (dill LIBRARY dill REQUIRED ${CERCS_FIND_OPTIONS})
find_package (Threads)
find_package (MPI)
find_package (NVML)

list (APPEND DEPLIBS "-L${ATL_LIB_DIR}" "${ATL_LIB_DIR}/libatl.la")
list (APPEND DEPLIBS "-L${FFS_LIB_DIR}" "${FFS_LIB_DIR}/libffs.la")
list (APPEND DEPLIBS "-L${DILL_LIB_DIR}" "${DILL_LIB_DIR}/libdill.la")
if (EXISTS "${ATL_LIB_DIR}/libatl.a")
  set (ATL_STATIC_LIB "${ATL_LIB_DIR}/libatl.a")
endif()
if (EXISTS "${FFS_LIB_DIR}/libffs.a")
  set (FFS_STATIC_LIB "${FFS_LIB_DIR}/libffs.a")
endif()
if (EXISTS "${DILL_LIB_DIR}/libdill.a")
  set (DILL_STATIC_LIB "${DILL_LIB_DIR}/libdill.a")
endif()

if (NVML_FOUND)
  list (APPEND INC_DIRS ${NVML_INCLUDE_DIR})
  list (APPEND LIB_DIRS ${NVML_LIB_DIR})
  list (APPEND BASE_LIBS ${NVML_LIBRARY})
  list (APPEND EVPATH_TRANSPORT_FULL_DEP "${NVML_LIBRARY}")
  list (APPEND EVPATH_TRANSPORT_STATIC_DEPS "${NVML_LIBRARY}")
endif(NVML_FOUND)

include(CheckLibraryExists)
CHECK_LIBRARY_EXISTS(rt clock_gettime "time.h" HAVE_CLOCK_GETTIME)

if (NOT HAVE_CLOCK_GETTIME)
   set(CMAKE_EXTRA_INCLUDE_FILES time.h)
   CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
   SET(CMAKE_EXTRA_INCLUDE_FILES)
else()
   list (APPEND BASE_LIBS rt)
endif()

list (APPEND INC_DIRS ${ATL_INCLUDE_DIR} ${FFS_INCLUDE_DIR})
list (APPEND LIB_DIRS ${CMAKE_CURRENT_BINARY_DIR} ${ATL_LIB_DIR} ${FFS_LIB_DIR} ${DILL_LIB_DIR})
list (APPEND BASE_LIBS ${FFS_LIBRARIES} ${ATL_LIBRARIES} ${DILL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} m )

list(REMOVE_DUPLICATES INC_DIRS)
FOREACH (DIR ${INC_DIRS})
  STRING (CONCAT CONFIG_INCLUDES "${CONFIG_INCLUDES}" " -I${DIR}")
ENDFOREACH()

INCLUDE_DIRECTORIES(${INC_DIRS} ${DL_INCLUDE_DIR})
LINK_DIRECTORIES(${LIB_DIRS})

SET (CM_DEFAULT_TRANSPORT "sockets")

if (NOT NO_TRANSPORT_MODULES)
  add_library(cmsockets MODULE cmsockets.c ip_config.c)
  add_library(cmselect MODULE cmselect.c)
  add_library(cmudp MODULE cmudp.c)
  add_library(cmmulticast MODULE cmmulticast.c)

  TARGET_LINK_LIBRARIES(cmselect ${BASE_LIBS})
  TARGET_LINK_LIBRARIES(cmsockets ${BASE_LIBS})
  TARGET_LINK_LIBRARIES(cmudp ${BASE_LIBS})
  TARGET_LINK_LIBRARIES(cmmulticast ${BASE_LIBS})
  list (APPEND TARGETS evpath cmselect cmsockets cmudp cmmulticast)
else  (NOT NO_TRANSPORT_MODULES)
  list (APPEND TARGETS evpath)
endif (NOT NO_TRANSPORT_MODULES)

FIND_CERCS_PROJECT (enet LIBRARY enet INCLUDES enet/enet.h ${CERCS_FIND_OPTIONS})
IF (NOT ENET_FOUND)
   MESSAGE (STATUS " - Enet library was not found.  This is not a fatal error, just that the Enet transport will not be built.")
ENDIF (NOT ENET_FOUND)

if (ENET_FOUND)
   INCLUDE_DIRECTORIES(${ENET_INCLUDE_DIR})
   LINK_DIRECTORIES(${ENET_LIB_DIR})
else (ENET_FOUND)
   set (ENET_LIBRARIES "")
endif (ENET_FOUND)

SET(RUN_NNTI_TESTS False)
SET(RUN_ENET_TESTS False)
SET(RUN_IB_TESTS False)
IF (ENET_FOUND)
   SET(RUN_ENET_TESTS True)
   If (NOT NO_TRANSPORT_MODULES)
      add_library(cmenet MODULE cmenet.c ip_config.c)
      list (APPEND TARGETS cmenet)
      TARGET_LINK_LIBRARIES(cmenet ${ENET_LIBRARIES} ${BASE_LIBS})
   ELSE (NOT NO_TRANSPORT_MODULES)
      list (APPEND EVPATH_SRC_LIST cmenet.c)  # link enet in main lib
      list (APPEND EVPATH_TRANSPORT_DEP_LIBS ${ENET_LIBRARIES} ${BASE_LIBS})
      list (APPEND DEPLIBS ${ENET_LIBRARIES})
      list (APPEND EVPATH_TRANSPORT_FULL_DEP "-L${ENET_LIB_DIR}" "${ENET_LIBRARIES}")
      if (EXISTS "${ENET_LIB_DIR}/libenet.a") 
	list (APPEND EVPATH_TRANSPORT_STATIC_DEPS "${ENET_LIB_DIR}/libenet.a") 
      endif ()
   ENDIF (NOT NO_TRANSPORT_MODULES)
ENDIF (ENET_FOUND)

CHECK_LIBRARY_EXISTS (ibverbs ibv_create_qp "" HAVE_IBVERBS)
CHECK_LIBRARY_EXISTS (ugni GNI_CqCreate "" HAVE_UGNI)
FIND_CERCS_PROJECT(libfabric LIBRARY fabric INCLUDES rdma/fabric.h ${CERCS_FIND_OPTIONS})
IF (LIBFABRIC_FOUND) 
   INCLUDE_DIRECTORIES(${LIBFABRIC_INCLUDE_DIR})
   LINK_DIRECTORIES(${LIBFABRIC_LIB_DIR})
   if (NOT NO_TRANSPORT_MODULES)
      add_library(cmfabric MODULE cmfabric.c ip_config.c)
      list (APPEND TARGETS cmfabric)
      TARGET_LINK_LIBRARIES(cmfabric fabric dl ${BASE_LIBS})
   else (NOT NO_TRANSPORT_MODULES)
      list (APPEND EVPATH_SRC_LIST cmfabric.c)  # link ib in main lib
      list (APPEND EVPATH_TRANSPORT_DEP_LIBS fabric dl ${BASE_LIBS})
      list (APPEND EVPATH_TRANSPORT_FULL_DEP "-L${LIBFABRIC_LIB_DIR}" "-lfabric" "-ldl")
      if (EXISTS "${ENET_LIB_DIR}/libfabric.a") 
	list (APPEND EVPATH_TRANSPORT_STATIC_DEPS "${ENET_LIB_DIR}/libfabric.a") 
      endif ()
      list (APPEND DEPLIBS fabric dl)
   endif (NOT NO_TRANSPORT_MODULES)
ENDIF(LIBFABRIC_FOUND)
IF (HAVE_IBVERBS)
   message( STATUS "Check for enough rlimit to run IB tests" )
   file( READ "${CMAKE_SOURCE_DIR}/cmake/GOOD_MEMLOCK_LIMIT.c" _SOURCE )
   CHECK_C_SOURCE_RUNS( "${_SOURCE}" GOOD_MEMLOCK_LIMIT )
   if( ${GOOD_MEMLOCK_LIMIT} )
	message( STATUS "Check for large enough MEMLOCK rlimit to run IB tests - yes" )
	SET(RUN_IB_TESTS True)
	SET(RUN_NNTI_TESTS False)
   else()
	message( STATUS "Check for large enough MEMLOCK rlimit to run IB tests - no")
	message( WARNING 
"Building InfiniBand transport, but current RLIMIT_MEMLOCK value " 
"will prevent successful runs.  Not running IB transport tests.")
   endif()
   if (NOT NO_TRANSPORT_MODULES)
      add_library(cmib MODULE cmib.c)
      list (APPEND TARGETS cmib)
      TARGET_LINK_LIBRARIES(cmib ibverbs ${BASE_LIBS})
   else (NOT NO_TRANSPORT_MODULES)
      list (APPEND EVPATH_SRC_LIST cmib.c)  # link ib in main lib
      list (APPEND EVPATH_TRANSPORT_DEP_LIBS ibverbs ${BASE_LIBS})
      list (APPEND EVPATH_TRANSPORT_FULL_DEP "-libverbs")
      list (APPEND DEPLIBS ibverbs)
      list (APPEND EVPATH_TRANSPORT_STATIC_DEPS "-libverbs") 
   endif (NOT NO_TRANSPORT_MODULES)
   set (IB_FOUND 1)
ENDIF (HAVE_IBVERBS)

if (NOT (DEFINED CercsArch))
   execute_process(COMMAND cercs_arch OUTPUT_VARIABLE CercsArch ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
   MARK_AS_ADVANCED(CercsArch)
endif()

IF (HAVE_IBVERBS OR HAVE_UGNI)
    FIND_CERCS_PROJECT (nnti LIBRARY trios_nnti INCLUDES Trios_nnti.h ${CERCS_FIND_OPTIONS})
    IF (NNTI_FOUND)
        INCLUDE_DIRECTORIES(${NNTI_INCLUDE_DIR})
	LINK_DIRECTORIES(${NNTI_LIB_DIR})
	if ("${CercsArch}" STREQUAL "ppc64")
	    SET(RUN_NNTI_TESTS False)
	endif ("${CercsArch}" STREQUAL "ppc64")
	IF (NOT NO_TRANSPORT_MODULES)
           add_library(cmnnti MODULE cmnnti.c)
	   list (APPEND TARGETS cmnnti)
	   STRING(REGEX REPLACE trios_nnti trios_support NNTI_SUP_LIB ${NNTI_LIBRARIES})
	   list (APPEND NNTI_LIBRARIES ${NNTI_SUP_LIB})
	   TARGET_LINK_LIBRARIES(cmnnti ${NNTI_LIBRARIES} ${ENET_LIBRARIES} ${BASE_LIBS})
	ELSE (NOT NO_TRANSPORT_MODULES)
	   list (APPEND EVPATH_SRC_LIST cmnnti.c)  # link nnti in main lib
	   STRING(REGEX REPLACE trios_nnti trios_support NNTI_SUP_LIB ${NNTI_LIBRARIES})
	   list (APPEND NNTI_LIBRARIES ${NNTI_SUP_LIB} -lstdc++ )
	   if ("${CercsArch}" STREQUAL "sith")
	       list (APPEND NNTI_LIBRARIES -libverbs)
	   endif ("${CercsArch}" STREQUAL "sith")
	   if ("${CercsArch}" STREQUAL "titan")
	       list (APPEND NNTI_LIBRARIES -lstdc++)
	   endif ("${CercsArch}" STREQUAL "titan")
	   
	   list (APPEND EVPATH_TRANSPORT_DEP_LIBS ${NNTI_LIBRARIES} ${ENET_LIBRARIES})
	   list (APPEND EVPATH_TRANSPORT_FULL_DEP "-L${NNTI_LIB_DIR}" "${NNTI_LIBRARIES}" "-L${ENET_LIB_DIR}" "${ENET_LIBRARIES}")
	   if (EXISTS "${NNTI__LIB_DIR}/libtrios_nnti.a") 
	     list (APPEND EVPATH_TRANSPORT_STATIC_DEPS "${NNTI_LIB_DIR}/libtrios_nnti.a" "${NNTI_LIB_DIR}/libtrios_support.a") 
	   endif ()
	   list (APPEND DEPLIBS ${NNTI_LIBRARIES})
        ENDIF (NOT NO_TRANSPORT_MODULES)
    ENDIF (NNTI_FOUND)
ENDIF (HAVE_IBVERBS OR HAVE_UGNI)

add_custom_command(
  OUTPUT "cm_interface.c" "revp.c" "revpath.h"
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h
  COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/gen_interface.pl ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h ${CMAKE_CURRENT_SOURCE_DIR}/cm_schedule.h
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_interface.pl ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h ${CMAKE_CURRENT_SOURCE_DIR}/cm_schedule.h
)

IF (Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
    set (USE_PTHREADS True)
ENDIF (Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)

CHECK_INCLUDE_FILE(hostlib.h HAVE_HOSTLIB_H)
CHECK_INCLUDE_FILE(malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILE(memory.h HAVE_MEMORY_H)
CHECK_INCLUDE_FILE(netdb.h HAVE_NETDB_H)
CHECK_INCLUDE_FILE(sockLib.h HAVE_SOCKLIB_H)
CHECK_INCLUDE_FILE(stdarg.h STDC_HEADERS)
CHECK_INCLUDE_FILE(stdint.h HAVE_STDINT_H)
CHECK_INCLUDE_FILE(stdlib.h HAVE_STDLIB_H)
CHECK_INCLUDE_FILE(string.h HAVE_STRING_H)
CHECK_INCLUDE_FILE(sys/select.h HAVE_SYS_SELECT_H)
CHECK_INCLUDE_FILE(sys/socket.h HAVE_SYS_SOCKET_H)
CHECK_INCLUDE_FILE(sys/sockio.h HAVE_SYS_SOCKIO_H)
CHECK_INCLUDE_FILE(sys/time.h HAVE_SYS_TIME_H)
CHECK_INCLUDE_FILE(sys/times.h HAVE_SYS_TIMES_H)
CHECK_INCLUDE_FILE(sys/uio.h HAVE_SYS_UIO_H)
CHECK_INCLUDE_FILE(sys/un.h HAVE_SYS_UN_H)
CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)
CHECK_INCLUDE_FILE(windows.h HAVE_WINDOWS_H)
CHECK_INCLUDE_FILE(winsock.h HAVE_WINSOCK_H)

CHECK_STRUCT_HAS_MEMBER("struct fd_set" "fds_bits" "sys/select.h" HAVE_FDS_BITS)

CHECK_TYPE_SIZE("int"   SIZEOF_INT)
CHECK_TYPE_SIZE("long"   SIZEOF_LONG)
TEST_BIG_ENDIAN(WORDS_BIGENDIAN)

CHECK_FUNCTION_EXISTS(writev HAVE_WRITEV)
CHECK_FUNCTION_EXISTS(uname HAVE_UNAME)
CHECK_FUNCTION_EXISTS(getdomainname HAVE_GETDOMAINNAME)
CHECK_FUNCTION_EXISTS(getloadavg HAVE_GETLOADAVG)
CHECK_FUNCTION_EXISTS(gettimeofday HAVE_GETTIMEOFDAY)
CHECK_FUNCTION_EXISTS(getifaddrs HAVE_GETIFADDRS)

TRY_COMPILE(HAVE_MAC_SYSCTL ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_mac_sysctl.c)
TRY_COMPILE(HAVE_SYSINFO ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_sysinfo.c)
TRY_COMPILE(HAVE_SYSCONF ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_sysconf.c)

foreach(arg ${DEPLIBS})
   set(DEP_STRING "${DEP_STRING} ${arg}")
endforeach(arg ${DEPLIBS})

if (TARGET_CNL)
  if (${CMAKE_CXX_COMPILER_ID} STREQUAL "PGI")
    string(REPLACE "-lstdc++" "" new_deps "${DEP_STRING}")
    set (DEP_STRING "${new_deps}")
    string(REPLACE ";-lstdc++" "" new_deps "${EVPATH_TRANSPORT_DEP_LIBS}")
    set (EVPATH_TRANSPORT_DEP_LIBS "${new_deps}")
  endif (${CMAKE_CXX_COMPILER_ID} STREQUAL "PGI")
endif (TARGET_CNL)

CHECK_BROKEN_TITAN_COMPILER(${DEP_STRING})

string(REPLACE ";" " " EVPATH_TRANSPORT_FULL_DEP_STR "${EVPATH_TRANSPORT_FULL_DEP}")
string(REPLACE ";" " " EVPATH_TRANSPORT_STATIC_DEP_STR "${EVPATH_TRANSPORT_STATIC_DEPS}")

add_libtool_library(NAME evpath SRC_LIST ${EVPATH_SRC_LIST} DEP_LIBS "${DEP_STRING}" LINK_LIBS  ${DL_LIBRARIES} ${BASE_LIBS} ${EVPATH_TRANSPORT_DEP_LIBS})

set (C_EXECUTABLE_LINKER_LANGUAGE "C")
if (TARGET_CNL)
  if (${CMAKE_CXX_COMPILER_ID} STREQUAL "PGI")
    string(REPLACE ";-lstdc++" "" new_deps "${evpath_LIB_DEPENDS}")
    set (evpath_LIB_DEPENDS "${new_deps}")
  endif (${CMAKE_CXX_COMPILER_ID} STREQUAL "PGI")
  set (C_EXECUTABLE_LINKER_LANGUAGE "CXX")
endif (TARGET_CNL)

if (${CMAKE_C_COMPILER_ID} MATCHES "Intel") 
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -shared-intel")
endif()

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cm_schedule.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/revpath.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cm_transport.h DESTINATION include)
INSTALL(TARGETS ${TARGETS} EXPORT EVPathExport
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

SET (EVPATH_LIBRARY_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
SET (EVPATH_LIBRARY_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib)

ADD_EXECUTABLE(cmprobe cmprobe.c)
TARGET_LINK_LIBRARIES(cmprobe evpath ${BASE_LIBS})

if (${CMAKE_C_COMPILER_ID} MATCHES "Intel") 
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -shared-intel")
endif()

find_program(SSH ssh)
if (SSH) 
   set (SSH_PATH "${SSH}")
endif (SSH) 

if (BUILD_TESTING) 
  ENABLE_TESTING()

  ADD_SUBDIRECTORY( tests )
  ADD_SUBDIRECTORY( rtests )
  ADD_SUBDIRECTORY( mtests )
  ADD_SUBDIRECTORY( dfg_tests )

  if (MPI_FOUND)
    ADD_SUBDIRECTORY( mpi_tests )
  endif (MPI_FOUND)

  ADD_SUBDIRECTORY( examples )

endif (BUILD_TESTING)

get_target_property_with_default(EVPATH_VERSION_MAJOR evpath LT_VERSION_CURRENT "0")
get_target_property_with_default(EVPATH_VERSION_MINOR evpath LT_VERSION_AGE "0")
get_target_property_with_default(EVPATH_VERSION_PATCH evpath LT_VERSION_REVISION "0")

set(EVPATH_VERSION ${EVPATH_VERSION_MAJOR}.${EVPATH_VERSION_MINOR}.${EVPATH_VERSION_PATCH} )

# set up install directories. INCLUDE_INSTALL_DIR and LIB_INSTALL_DIR must not be absolute paths
set(LIB_INSTALL_DIR_SUFFIX "" CACHE STRING "The directories where to install libraries to")
set(LIB_INSTALL_DIR lib${LIB_INSTALL_DIR_SUFFIX} )
set(CMAKECONFIG_INSTALL_DIR ${LIB_INSTALL_DIR}/cmake/EVPath ) 

# Use a versioned install directory for the headers so multiple versions can be installed in parallel
set(INCLUDE_INSTALL_DIR include )

# not changable, but we set a variable anyway for consistency
set(BIN_INSTALL_DIR bin)

# This "exports" all targets which have been put into the export set "EVPathExport".
# This means that cmake generates a file with the given filename, which can later on be loaded
# by projects using this package.
# This file contains add_library(EVPath IMPORTED) statements for each target in the export set, so
# when loaded later on cmake will create "imported" library targets from these, which can be used
# in many ways in the same way as a normal library target created via a normal add_library().
install(EXPORT EVPathExport DESTINATION ${CMAKECONFIG_INSTALL_DIR} FILE EVPathTargets.cmake )

# figure out the relative path from the installed Config.cmake file to the install prefix (which may be at
# runtime different from the chosen CMAKE_INSTALL_PREFIX if under Windows the package was installed anywhere)
# This relative path will be configured into the EVPathConfig.cmake
file(RELATIVE_PATH relInstallDir ${CMAKE_INSTALL_PREFIX}/${CMAKECONFIG_INSTALL_DIR} ${CMAKE_INSTALL_PREFIX} )

# Create a EVPathConfig.cmake file. <name>Config.cmake files are searched by find_package()
# automatically. We configure that file so that we can put any information we want in it,
# e.g. version numbers, include directories, etc.
configure_file(EVPathConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/EVPathConfig.cmake @ONLY )

# Create a EVPathConfig.cmake file. <name>Config.cmake files are searched by find_package()
# automatically. We configure that file so that we can put any information we want in it,
# e.g. version numbers, include directories, etc.
configure_file(evpath_config.in ${CMAKE_CURRENT_BINARY_DIR}/evpath_config @ONLY )
install(PROGRAMS ${CMAKE_BINARY_DIR}/evpath_config DESTINATION bin)

# Additionally, when cmake has found a EVPathConfig.cmake, it can check for a EVPathConfigVersion.cmake
# in the same directory when figuring out the version of the package when a version
# has been specified in the find_package() call, e.g. find_package(EVPath 1.0)
configure_file(EVPathConfigVersion.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/EVPathConfigVersion.cmake @ONLY )

# Install these two files into the same directory as the generated exports-file.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/EVPathConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/EVPathConfigVersion.cmake
        DESTINATION ${CMAKECONFIG_INSTALL_DIR} )


CONFIGURE_FILE( ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )

if (TEST_INSTALL_DIRECTORY) 
   SET (EVPATH_TEST_INSTALL_DIR ${TEST_INSTALL_DIRECTORY})
endif()

# display status message for important variables
MESSAGE( STATUS )
MESSAGE( STATUS "-------------------------------------------------------------------------------" )
MESSAGE( STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}" )
MESSAGE( STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}  (options are: Debug,Release,RelWithDebInfo,MinSizeRel)")
MESSAGE( STATUS "BUILD_SHARED_STATIC = ${BUILD_SHARED_STATIC}" )
MESSAGE( STATUS "Change a value with: cmake -D<Variable>=<Value>" )
MESSAGE( STATUS "    to use installed CERCS libraries specify -DCERCS_USE_INSTALLED=1" )
MESSAGE( STATUS "-------------------------------------------------------------------------------" )

