
AUTOMAKE_OPTIONS = no-dependencies foreign

TESTS = cmtest cmping bulktest take_test

check-local:
	@if CMTransport=udp ./cmtest$(EXEEXT) ; then\
	 echo "PASS: cmtest(udp)";\
	else echo "FAIL: cmtest(udp)"; fi
	@if CMTransport=udp ./cmping$(EXEEXT) ; then\
	 echo "PASS: cmping(udp)";\
	else echo "FAIL: cmping(udp)"; fi

noinst_PROGRAMS = cmtest cmping bulktest take_test cmconn

LDFLAGS = ../libevpath.la

DEPENDENCIES = ../libevpath.la
