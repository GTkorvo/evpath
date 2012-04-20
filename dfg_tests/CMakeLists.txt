cmake_minimum_required(VERSION 2.8)

set (TESTS evtest rawtest chain_test filter_test tree_test multi_test dchain_test fail_chain_test reconfig_chain_test )

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(dfg_${TEST} ${TEST}.c test_support.c)
    TARGET_LINK_LIBRARIES(dfg_${TEST} evpath)
    ADD_TEST(dfg_${TEST} dfg_${TEST})
endforeach()


