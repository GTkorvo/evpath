cmake_minimum_required(VERSION 2.8)

set (TESTS evtest extract_test remote_terminal_test)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(revp_${TEST} ${TEST}.c)
    TARGET_LINK_LIBRARIES(revp_${TEST} evpath ${BASE_LIBS})
    ADD_TEST(revp_${TEST} "revp_${TEST}")
endforeach()


