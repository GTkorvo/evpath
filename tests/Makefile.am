
AUTOMAKE_OPTIONS = no-dependencies foreign

OLD_TESTS = cmtest cmping take_test
TESTS = evtest bulktest http_test filter_test router_test router_test2 filter2_test transform_test auto_test extract_test submit_test

check-local:
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi
	@if CMNonBlockWrite=1 CMReadAheadMsgLimit=1 ./block_test$(EXEEXT) ; then\
	 echo "PASS: block_test";\
	else echo "FAIL: block_test"; fi

noinst_PROGRAMS = evtest bulktest http_test filter_test router_test router_test2 filter2_test transform_test auto_test extract_test block_test submit_test

LDADD = ../libevpath.la

LDFLAGS = -static
