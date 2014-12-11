cmake_minimum_required(VERSION 2.8)

set (TESTS evtest master_not_client multi_sink rawtest chain_test filter_test tree_test multi_test dchain_test fail_chain_test 
  reconfig_chain_test anon_multi_test auto_tree_test router_test force_test self_reconfig_test source_split named_stones)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dfg_tests.tsts "" )
foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(dfg_${TEST} ${TEST}.c test_support.c)
    SET_TARGET_PROPERTIES(dfg_${TEST} PROPERTIES LINKER_LANGUAGE ${C_EXECUTABLE_LINKER_LANGUAGE})
    TARGET_LINK_LIBRARIES(dfg_${TEST} evpath ${BASE_LIBS})
    ADD_TEST(dfg_${TEST} dfg_${TEST})
    LIST( APPEND DFG_TESTS "dfg_${TEST}")
    file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/dfg_tests.tsts "dfg_${TEST}\n" )
endforeach()

ADD_TEST(dfg_chain_test_one_proc dfg_chain_test 1)
ADD_TEST(dfg_dchain_test_one_proc dfg_dchain_test 1)

if (TEST_INSTALL_DIRECTORY) 
  install(TARGETS ${DFG_TESTS} DESTINATION "${TEST_INSTALL_DIRECTORY}")
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dfg_tests.tsts DESTINATION "${TEST_INSTALL_DIRECTORY}")
endif (TEST_INSTALL_DIRECTORY) 
