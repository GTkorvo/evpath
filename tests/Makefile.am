
AUTOMAKE_OPTIONS = no-dependencies foreign

OLD_TESTS = cmtest cmping take_test
TESTS = evtest bulktest

check-local:
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi

noinst_PROGRAMS = evtest bulktest

LDADD = ../libevpath.la
