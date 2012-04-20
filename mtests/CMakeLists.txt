cmake_minimum_required(VERSION 2.8)

set (TESTS cmtest cmping bulktest take_test)

ENABLE_TESTING()

INCLUDE_DIRECTORIES(BEFORE ${evpath_BINARY_DIR} ${evpath_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

foreach (TEST ${TESTS} )
    ADD_EXECUTABLE(mtests_${TEST} ${TEST}.c)
    TARGET_LINK_LIBRARIES(mtests_${TEST} evpath)
    ADD_TEST(mtests_${TEST} mtests_${TEST})
endforeach()

ADD_TEST (mtests_cmtest_UDP mtests_cmtest -t udp)
ADD_TEST (mtests_cmping_UDP mtests_cmping -t udp)
