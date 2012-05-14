cmake_minimum_required(VERSION 2.8)
project(evpath)

IF( NOT CMAKE_BUILD_TYPE )
SET( CMAKE_BUILD_TYPE "RelWithDebInfo" )
ENDIF()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(CheckFunctionExists)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckTypeSize)
include(CheckStructHasMember)
include(FindCERCSProject)
include(CTest)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set (EVPATH_SRC_LIST cm.c cm_control.c cm_formats.c cm_util.c cm_transport.c 
                       cm_lock.c cm_perf.c cm_pbio.c cm_interface.c version.c
                       cm_threadio.c cm_evol.c evp.c response.c metrics.c dlloader.c
                       revp.c evp_compat.c thin_server.c evp_threads.c ev_dfg.c)


IF (TARGET_CNL)   # This should be set to true if compiling for compute note linux
  set (NO_DYNAMIC_LINKING 1)   # dummy dyn linking in cm.c and cm_transport.c
  list (APPEND EVPATH_SRC_LIST cmsockets.c cmselect.c)  # link sockets and select mechanisms in main lib
ENDIF (TARGET_CNL)

add_library(evpath SHARED ${EVPATH_SRC_LIST})
add_library(evpath-static STATIC ${EVPATH_SRC_LIST})
# The library target "evpath" already has a default OUTPUT_NAME of "evpath", so we don't need to change it.
# The library target "evpath-static" has a default OUTPUT_NAME of "evpath-static", so change it.
SET_TARGET_PROPERTIES(evpath-static PROPERTIES OUTPUT_NAME "evpath" )
# Now the library target "foo-static" will be named "foo.lib" with MS tools.
# This conflicts with the "foo.lib" import library corresponding to "foo.dll",
# so we add a "lib" prefix (which is default on other platforms anyway):
SET_TARGET_PROPERTIES(evpath-static PROPERTIES PREFIX "lib" LINKER_LANGUAGE C)
SET_TARGET_PROPERTIES(evpath PROPERTIES LINKER_LANGUAGE C)

add_custom_command(
  OUTPUT "cm_interface.c" "revp.c" "revpath.h"
  COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/gen_interface.pl
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_interface.pl
)

if (DEFINED CERCS_USE_INSTALLED) 
   set (CERCS_USE_INSTALLED "USE_INSTALLED")
endif(DEFINED CERCS_USE_INSTALLED) 

find_package(DL)
FIND_CERCS_PROJECT (cercs_env LIBRARY cercs_env INCLUDES cercs_env.h REQUIRED ${CERCS_USE_INSTALLED})
FIND_CERCS_PROJECT (atl LIBRARY atl INCLUDES atl.h REQUIRED ${CERCS_USE_INSTALLED})
FIND_CERCS_PROJECT (gen_thread LIBRARY gen_thread INCLUDES gen_thread.h REQUIRED ${CERCS_USE_INSTALLED})
FIND_CERCS_PROJECT (ffs LIBRARY ffs INCLUDES ffs.h REQUIRED ${CERCS_USE_INSTALLED})
FIND_CERCS_PROJECT (dill LIBRARY dill REQUIRED ${CERCS_USE_INSTALLED})
find_package (Threads)

list (APPEND INC_DIRS ${CERCS_ENV_INCLUDE_DIR} ${ATL_INCLUDE_DIR} ${GEN_THREAD_INCLUDE_DIR} ${FFS_INCLUDE_DIR})
list (APPEND LIB_DIRS ${CERCS_ENV_LIB_DIR} ${ATL_LIB_DIR} ${GEN_THREAD_LIB_DIR} ${FFS_LIB_DIR} ${DILL_LIB_DIR})
list (APPEND BASE_LIBS ${CERCS_ENV_LIBRARIES} ${ATL_LIBRARIES} ${GEN_THREAD_LIBRARIES} ${FFS_LIBRARIES} ${DILL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

INCLUDE_DIRECTORIES(${INC_DIRS} ${DL_INCLUDE_DIR})
LINK_DIRECTORIES(${LIB_DIRS})

add_library(cmsockets MODULE cmsockets.c)
add_library(cmselect MODULE cmselect.c)
add_library(cmudp MODULE cmudp.c)
add_library(cmmulticast MODULE cmmulticast.c)

TARGET_LINK_LIBRARIES(evpath m ${DL_LIBRARIES} ${BASE_LIBS})
TARGET_LINK_LIBRARIES(evpath-static m ${DL_LIBRARIES} ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmselect ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmsockets ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmudp ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmmulticast ${BASE_LIBS})

FIND_CERCS_PROJECT (enet LIBRARY enet INCLUDES enet/enet.h ${CERCS_USE_INSTALLED})
IF (ENET_FOUND)
   add_library(cmenet MODULE cmenet.c)
   INCLUDE_DIRECTORIES(${ENET_INCLUDE_DIR})
   LINK_DIRECTORIES(${ENET_LIB_DIR})
   TARGET_LINK_LIBRARIES(cmenet ${ENET_LIBRARIES} ${BASE_LIBS})
ENDIF (ENET_FOUND)

CHECK_LIBRARY_EXISTS (ibverbs ibv_create_qp "" HAVE_IBVERBS)
IF (HAVE_IBVERBS)
   add_library(cmib MODULE cmib.c)
   TARGET_LINK_LIBRARIES(cmib ibverbs ${BASE_LIBS})
ENDIF (HAVE_IBVERBS)

IF (HAVE_IBVERBS)
    FIND_CERCS_PROJECT (nnti LIBRARY trios_nssi INCLUDES nnti.h ${CERCS_USE_INSTALLED})
    IF (NNTI_FOUND)
        add_library(cmnnti MODULE cmnnti.c)
	INCLUDE_DIRECTORIES(${NNTI_INCLUDE_DIR})
	LINK_DIRECTORIES(${NNTI_LIB_DIR})
	TARGET_LINK_LIBRARIES(cmnnti ${NNTI_LIBRARIES} ${BASE_LIBS})
      ENDIF (NNTI_FOUND)
ENDIF (HAVE_IBVERBS)

IF (Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)
    set (USE_PTHREADS True)
ENDIF (Threads_FOUND AND CMAKE_USE_PTHREADS_INIT)

CHECK_INCLUDE_FILE(hostlib.h HAVE_HOSTLIB_H)
CHECK_INCLUDE_FILE(malloc.h HAVE_MALLOC_H)
CHECK_INCLUDE_FILE(memory.h HAVE_MEMORY_H)
CHECK_INCLUDE_FILE(netdb.h HAVE_NETDB_H)
CHECK_INCLUDE_FILE(sockLib.h HAVE_SOCKLIB_H)
CHECK_INCLUDE_FILE(stdarg.h STDC_HEADERS)
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

CHECK_FUNCTION_EXISTS(writev HAVE_WRITEV)
CHECK_FUNCTION_EXISTS(uname HAVE_UNAME)
CHECK_FUNCTION_EXISTS(getdomainname HAVE_GETDOMAINNAME)
CHECK_FUNCTION_EXISTS(getloadavg HAVE_GETLOADAVG)
CHECK_FUNCTION_EXISTS(gettimeofday HAVE_GETTIMEOFDAY)

TRY_COMPILE(HAVE_MAC_SYSCTL ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_mac_sysctl.c)
TRY_COMPILE(HAVE_LINUX_SYSCTL ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_mac_sysctl.c)

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/revpath.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cm_transport.h DESTINATION include)
INSTALL(TARGETS evpath evpath-static cmselect cmsockets cmudp cmmulticast
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

SET (EVPATH_LIBRARY_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
SET (EVPATH_LIBRARY_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib)

CONFIGURE_FILE( ${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h )

# display status message for important variables
MESSAGE( STATUS )
MESSAGE( STATUS "-------------------------------------------------------------------------------" )
MESSAGE( STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}" )
MESSAGE( STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}  (options are: Debug,Release,RelWithDebInfo,MinSizeRel)")
MESSAGE( STATUS "Change a value with: cmake -D<Variable>=<Value>" )
MESSAGE( STATUS "    to use installed CERCS libraries specify -DCERCS_USE_INSTALLED=1" )
MESSAGE( STATUS "-------------------------------------------------------------------------------" )

ENABLE_TESTING()

ADD_SUBDIRECTORY( tests )
ADD_SUBDIRECTORY( rtests )
ADD_SUBDIRECTORY( mtests )
ADD_SUBDIRECTORY( dfg_tests )
ADD_SUBDIRECTORY( examples )
