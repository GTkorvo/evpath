cmake_minimum_required(VERSION 2.8)

set (TESTS simple_cm bulk_cm )


include_directories(${MPI_INCLUDE_PATH})


ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(${TEST} ${TEST}.c)
    TARGET_LINK_LIBRARIES(${TEST} evpath ${MPI_C_LIBRARIES} ${BASE_LIBS})

    if(MPI_COMPILE_FLAGS)
      set_target_properties(${TEST} PROPERTIES COMPILE_FLAGS "${MPI_C_COMPILE_FLAGS}")
    endif()

    if(MPI_LINK_FLAGS)
      set_target_properties(${TEST} PROPERTIES LINK_FLAGS "${MPI_C_LINK_FLAGS}")
    endif()
endforeach()

