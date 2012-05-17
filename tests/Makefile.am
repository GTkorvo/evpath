
AUTOMAKE_OPTIONS = no-dependencies foreign
LDFLAGS = @LDFLAGS@
SUBDIRS = testdll .

INCLUDES = -I$(srcdir)/.. -I..

OLD_TESTS = cmtest cmping take_test store_limit_test store_send_test congest_bulktest

TESTS = evtest bulktest http_test filter_test router_test router_test2 no_type_router_test filter2_test transform_test auto_test extract_test submit_test thin_test rawtest multi_thread multiq_test

check-local: block_test evtest check-enet
	@if CMTransport=udp ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(udp)";\
	else echo "FAIL: evtest(udp)"; fi
	@if CMNonBlockWrite=1 CMReadAheadMsgLimit=1 ./block_test$(EXEEXT) ; then\
	 echo "PASS: block_test";\
	else echo "FAIL: block_test"; fi

if BUILD_ENET
check-enet: evtest bulktest
	@if CMTransport=enet ./evtest$(EXEEXT) ; then\
	 echo "PASS: evtest(enet)";\
	else echo "FAIL: evtest(enet)"; fi
	@if CMTransport=enet ./bulktest$(EXEEXT) ; then\
	 echo "PASS: bulktest(enet)";\
	else echo "FAIL: bulktest(enet)"; fi
else
check-enet:
endif

noinst_PROGRAMS = $(TESTS) thin_client congestion_test congest_bulktest multiq_test store_send_test store_limit_test store_pressure_test block_test submit_wait_test multi_thread

LDADD = ../libevpath.la ../libcmsockets.la  

thin_client_LDADD = @FFSLINKARG@ -lffs
thin_test_DEPENDENCIES = ../libevpath.la thin_client
