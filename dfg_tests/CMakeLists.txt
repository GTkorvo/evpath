cmake_minimum_required(VERSION 2.8)

set (TESTS evtest master_not_client multi_sink rawtest chain_test filter_test tree_test multi_test dchain_test fail_chain_test 
  reconfig_chain_test anon_multi_test auto_tree_test router_test force_test self_reconfig_test source_split named_stones)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(dfg_${TEST} ${TEST}.c test_support.c)
    TARGET_LINK_LIBRARIES(dfg_${TEST} evpath ${BASE_LIBS})
    ADD_TEST(dfg_${TEST} dfg_${TEST})
endforeach()

ADD_TEST(dfg_chain_test_one_proc dfg_chain_test 1)
ADD_TEST(dfg_dchain_test_one_proc dfg_dchain_test 1)

