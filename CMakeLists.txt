cmake_minimum_required(VERSION 2.8.3)
cmake_policy(VERSION 2.8.3)
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
include(CreateLibtoolFile)
include(FindCERCSProject)
include(CTest)

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

set (EVPATH_SRC_LIST cm.c cm_control.c cm_formats.c cm_util.c cm_transport.c 
                       cm_lock.c cm_perf.c cm_pbio.c cm_interface.c version.c
                       cm_threadio.c cm_evol.c evp.c response.c metrics.c dlloader.c
                       revp.c evp_compat.c thin_server.c evp_threads.c ev_dfg.c)

set (CERCS_FIND_OPTIONS)
set (EVPATH_TRANSPORT_DEP_LIBS)

IF (TARGET_CNL)   # This should be set to true if compiling for compute note linux
  set (NO_DYNAMIC_LINKING 1)   # dummy dyn linking in cm.c and cm_transport.c
  list (APPEND EVPATH_SRC_LIST cmsockets.c cmselect.c)  # link sockets and select mechanisms in main lib
  list (APPEND CERCS_FIND_OPTIONS "STATIC")
ENDIF (TARGET_CNL)

if (DEFINED CERCS_USE_INSTALLED) 
   set (CERCS_USE_INSTALLED)
   list (APPEND CERCS_FIND_OPTIONS "USE_INSTALLED")
endif(DEFINED CERCS_USE_INSTALLED) 

find_package(DL)
FIND_CERCS_PROJECT (cercs_env LIBRARY cercs_env INCLUDES cercs_env.h REQUIRED ${CERCS_FIND_OPTIONS})
FIND_CERCS_PROJECT (atl LIBRARY atl INCLUDES atl.h REQUIRED ${CERCS_FIND_OPTIONS})
FIND_CERCS_PROJECT (gen_thread LIBRARY gen_thread INCLUDES gen_thread.h REQUIRED ${CERCS_FIND_OPTIONS})
FIND_CERCS_PROJECT (ffs LIBRARY ffs INCLUDES ffs.h REQUIRED ${CERCS_FIND_OPTIONS})
FIND_CERCS_PROJECT (dill LIBRARY dill REQUIRED ${CERCS_FIND_OPTIONS})
find_package (Threads)

list (APPEND DEPLIBS "-L${CERCS_ENV_LIB_DIR}" "${CERCS_ENV_LIB_DIR}/libcercs_env.la")
list (APPEND DEPLIBS "-L${ATL_LIB_DIR}" "${ATL_LIB_DIR}/libatl.la")
list (APPEND DEPLIBS "-L${GEN_THREAD_LIB_DIR}" "${GEN_THREAD_LIB_DIR}/libgen_thread.la")
list (APPEND DEPLIBS "-L${FFS_LIB_DIR}" "${FFS_LIB_DIR}/libffs.la")
list (APPEND DEPLIBS "-L${DILL_LIB_DIR}" "${DILL_LIB_DIR}/libdill.la")

list (APPEND INC_DIRS ${CERCS_ENV_INCLUDE_DIR} ${ATL_INCLUDE_DIR} ${GEN_THREAD_INCLUDE_DIR} ${FFS_INCLUDE_DIR})
list (APPEND LIB_DIRS ${CMAKE_CURRENT_BINARY_DIR} ${CERCS_ENV_LIB_DIR} ${ATL_LIB_DIR} ${GEN_THREAD_LIB_DIR} ${FFS_LIB_DIR} ${DILL_LIB_DIR})
list (APPEND BASE_LIBS m ${CERCS_ENV_LIBRARIES} ${ATL_LIBRARIES} ${GEN_THREAD_LIBRARIES} ${FFS_LIBRARIES} ${DILL_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
set (TARGETS evpath-static)

INCLUDE_DIRECTORIES(${INC_DIRS} ${DL_INCLUDE_DIR})
LINK_DIRECTORIES(${LIB_DIRS})

if (NOT TARGET_CNL)
add_library(cmsockets MODULE cmsockets.c)
add_library(cmselect MODULE cmselect.c)
add_library(cmudp MODULE cmudp.c)
add_library(cmmulticast MODULE cmmulticast.c)

TARGET_LINK_LIBRARIES(cmselect ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmsockets ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmudp ${BASE_LIBS})
TARGET_LINK_LIBRARIES(cmmulticast ${BASE_LIBS})
list (APPEND TARGETS evpath cmselect cmsockets cmudp cmmulticast)
endif ( NOT TARGET_CNL)

FIND_CERCS_PROJECT (df_shm LIBRARY df_shm INCLUDES df_shm.h df_shm_queue.h ${CERCS_FIND_OPTIONS})
IF (NOT DF_SHM_FOUND)
   MESSAGE (STATUS " - Df_shm library was not found.  This is not a fatal error, just that the NNTI/SHM transport will not be built.")
ENDIF (NOT DF_SHM_FOUND)

if (DF_SHM_FOUND)
   INCLUDE_DIRECTORIES(${DF_SHM_INCLUDE_DIR})
   LINK_DIRECTORIES(${DF_SHM_LIB_DIR})
endif (DF_SHM_FOUND)

FIND_CERCS_PROJECT (enet LIBRARY enet INCLUDES enet/enet.h ${CERCS_FIND_OPTIONS})
IF (NOT ENET_FOUND)
   MESSAGE (STATUS " - Enet library was not found.  This is not a fatal error, just that the Enet transport will not be built.")
ENDIF (NOT ENET_FOUND)

if (ENET_FOUND)
   INCLUDE_DIRECTORIES(${ENET_INCLUDE_DIR})
   LINK_DIRECTORIES(${ENET_LIB_DIR})
endif (ENET_FOUND)
IF (ENET_FOUND AND NOT TARGET_CNL)
   add_library(cmenet MODULE cmenet.c)
   TARGET_LINK_LIBRARIES(cmenet ${ENET_LIBRARIES} ${BASE_LIBS})
ENDIF (ENET_FOUND AND NOT TARGET_CNL)

CHECK_LIBRARY_EXISTS (ibverbs ibv_create_qp "" HAVE_IBVERBS)
CHECK_LIBRARY_EXISTS (ugni GNI_CqCreate "" HAVE_UGNI)
IF (HAVE_IBVERBS AND NOT TARGET_CNL)
   add_library(cmib MODULE cmib.c)
   TARGET_LINK_LIBRARIES(cmib ibverbs ${BASE_LIBS})
ENDIF (HAVE_IBVERBS AND NOT TARGET_CNL)

IF (HAVE_IBVERBS OR HAVE_UGNI)
    FIND_CERCS_PROJECT (nnti LIBRARY trios_nnti INCLUDES Trios_nnti.h ${CERCS_FIND_OPTIONS} VERBOSE )
    IF (NNTI_FOUND)
        INCLUDE_DIRECTORIES(${NNTI_INCLUDE_DIR})
	LINK_DIRECTORIES(${NNTI_LIB_DIR})
	IF (NOT TARGET_CNL)
           add_library(cmnnti MODULE cmnnti.c)
	   TARGET_LINK_LIBRARIES(cmnnti ${NNTI_LIBRARIES} ${ENET_LIBRARIES} ${DF_SHM_LIBRARIES} ${BASE_LIBS})
	ELSE (NOT TARGET_CNL)
	   list (APPEND EVPATH_SRC_LIST cmnnti.c)  # link nnti in main lib
	   list (APPEND NNTI_LIBRARIES trios_support)
	   list (APPEND EVPATH_TRANSPORT_DEP_LIBS ${NNTI_LIBRARIES} ${ENET_LIBRARIES} ${DF_SHM_LIBRARIES})
        ENDIF (NOT TARGET_CNL)
    ENDIF (NNTI_FOUND)
ENDIF (HAVE_IBVERBS OR HAVE_UGNI)

if (NOT TARGET_CNL)
add_library(evpath SHARED ${EVPATH_SRC_LIST})
SET_TARGET_PROPERTIES(evpath PROPERTIES LINKER_LANGUAGE C)
TARGET_LINK_LIBRARIES(evpath ${DL_LIBRARIES} ${BASE_LIBS})
SET_TARGET_PROPERTIES(evpath PROPERTIES STATIC_LIB "libevpath.a")
SET_TARGET_PROPERTIES(evpath PROPERTIES LT_SHOULDNOTLINK "no")
endif (NOT TARGET_CNL)
add_library(evpath-static STATIC ${EVPATH_SRC_LIST})
# The library target "evpath" already has a default OUTPUT_NAME of "evpath", so we don't need to change it.
# The library target "evpath-static" has a default OUTPUT_NAME of "evpath-static", so change it.
SET_TARGET_PROPERTIES(evpath-static PROPERTIES OUTPUT_NAME "evpath" )
# Now the library target "foo-static" will be named "foo.lib" with MS tools.
# This conflicts with the "foo.lib" import library corresponding to "foo.dll",
# so we add a "lib" prefix (which is default on other platforms anyway):
SET_TARGET_PROPERTIES(evpath-static PROPERTIES PREFIX "lib" LINKER_LANGUAGE C)

add_custom_command(
  OUTPUT "cm_interface.c" "revp.c" "revpath.h"
  SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h
  COMMAND perl ${CMAKE_CURRENT_SOURCE_DIR}/gen_interface.pl
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gen_interface.pl ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h
)

TARGET_LINK_LIBRARIES(evpath-static ${DL_LIBRARIES} ${BASE_LIBS} ${EVPATH_TRANSPORT_DEP_LIBS})

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

if (NOT TARGET_CNL)
   foreach(arg ${DEPLIBS})
      set(DEP_STRING "${DEP_STRING} ${arg}")
   endforeach(arg ${DEPLIBS})

  SET_TARGET_PROPERTIES(evpath PROPERTIES LT_DEPENDENCY_LIBS "${DEP_STRING}")
  CREATE_LIBTOOL_FILE(evpath /lib)
endif (NOT TARGET_CNL)

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/evpath.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/revpath.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ev_dfg.h DESTINATION include)
INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/cm_transport.h DESTINATION include)
INSTALL(TARGETS ${TARGETS}
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

SET (EVPATH_LIBRARY_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
SET (EVPATH_LIBRARY_INSTALL_DIR ${CMAKE_INSTALL_PREFIX}/lib)

ADD_EXECUTABLE(cmprobe cmprobe.c)
TARGET_LINK_LIBRARIES(cmprobe evpath-static ${BASE_LIBS})

if (${CMAKE_C_COMPILER_ID} MATCHES "Intel") 
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -shared-intel")
endif()

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
