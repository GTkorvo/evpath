
AUTOMAKE_OPTIONS = no-dependencies foreign

SUBDIRS = testdll .

OLD_TESTS = cmtest cmping take_test store_limit_test store_send_test congest_bulktest

TESTS = evtest bulktest http_test filter_test router_test router_test2 filter2_test transform_test auto_test extract_test submit_test thin_test rawtest multi_thread multiq_test

check-local: block_test evtest
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi
	@if CMNonBlockWrite=1 CMReadAheadMsgLimit=1 ./block_test$(EXEEXT) ; then\
	 echo "PASS: block_test";\
	else echo "FAIL: block_test"; fi

noinst_PROGRAMS = $(TESTS) thin_client congestion_test congest_bulktest multiq_test store_send_test store_limit_test store_pressure_test block_test submit_wait_test multi_thread autostone

LDADD = ../libevpath.la 

LDFLAGS = -static

thin_client_LDADD = -L@FFSDIR@ -lffs
thin_test_DEPENDENCIES = ../libevpath.la thin_client
