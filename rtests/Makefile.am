
AUTOMAKE_OPTIONS = no-dependencies foreign

TESTS = 

check-local:
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi

noinst_PROGRAMS = $(TESTS) 

LDADD = ../libevpath.la

LDFLAGS = -static

