
AUTOMAKE_OPTIONS = no-dependencies foreign

OLD_TESTS = cmtest cmping bulktest take_test
TESTS = evtest

check-local:
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi
	@if CMTransport=udp ./evping$(EXEEXT) ; then\
	 echo "PASS: evping(udp)";\
	else echo "FAIL: evping(udp)"; fi

noinst_PROGRAMS = evtest

LDFLAGS = -L.. -levpath

DEPENDENCIES = ../libevpath.la
