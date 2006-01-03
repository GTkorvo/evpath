
AUTOMAKE_OPTIONS = no-dependencies foreign

OLD_TESTS = cmtest cmping take_test
TESTS = evtest bulktest http_test filter_test router_test router_test2 filter2_test transform_test auto_test extract_test

check-local:
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi

noinst_PROGRAMS = evtest bulktest http_test filter_test router_test router_test2 filter2_test transform_test auto_test extract_test

LDADD = ../libevpath.la

LDFLAGS = -static
