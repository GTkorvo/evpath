
AUTOMAKE_OPTIONS = no-dependencies foreign

OLD_TESTS = cmtest cmping bulktest take_test
TESTS = evtest

check-local:
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi

noinst_PROGRAMS = evtest

LDFLAGS = -L.. -levpath

DEPENDENCIES = ../libevpath.la
