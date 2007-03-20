## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = libltdl . tests rtests examples

INCLUDES = $(INCLTDL)

EXTRA_DIST = 

lib_LTLIBRARIES = libevpath.la

EXTRA_LTLIBRARIES = 

LIBS=@LIBS@
LTLIBS=@LTLIBS@

libevpath_la_SOURCES = cm.c cm_control.c cm_formats.c cm_util.c cm_transport.c\
		       cm_lock.c cm_pbio.c cm_evol.c evp.c response.c \
		       cm_interface.c thin_server.c version.c revp.c

libevpath_la_LDFLAGS = $(MATH_LIB)
libevpath_la_LIBADD =  $(LIBLTDL) -lgen_thread -lcercs_env $(LTLIBS) -lIO -lecl @SOCKET_LIBS@ @KERNEL_OBJS@ 
libevpath_la_DEPENDENCIES = $(LIBLTDL) $(LTLIBS) @KERNEL_OBJS@

include_HEADERS = evpath.h revpath.h

AM_CPPFLAGS =

noinst_HEADERS = assert.h cm_internal.h  ev_internal.h

cm_interface.c: evpath.h gen_interface.pl
	@PERL@ ./gen_interface.pl

if KERNEL
bin_PROGRAMS = 
BUILT_SOURCES = env.c kcm.c presym.c
install-exec-local: cm_module.o
		install -d /lib/modules/@KERNEL_VERSION@/misc /lib/modules/misc
		install -c cm_module.o /lib/modules/@KERNEL_VERSION@/misc
		install -c cm_module.o /lib/modules/misc
all-local: cm_module.o
check-local: kernel_check_target
MOSTLYCLEANFILES = cm_module.o
else
bin_PROGRAMS =
all-local:
check-local:
install-exec-local:
noinst_PROGRAMS = 
MOSTLYCLEANFILES = cm_interface.c
endif


libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck

cm.::

kernel_check_target: cm_module.o kernel/undefined_syms
	nm cm_module.o | grep U > /tmp/cur_undefined_syms
	cmp /tmp/cur_undefined_syms kernel/undefined_syms
	rm /tmp/cur_undefined_syms

cm_module.o:	libcm.la libcmsockets.la libcmselect.la
	rm -fr /tmp/cm_build
	mkdir /tmp/cm_build
	cp .libs/libcm.a /tmp/cm_build
	cp .libs/libcmselect.a /tmp/cm_build
	cp .libs/libcmsockets.a /tmp/cm_build
	cd /tmp/cm_build ; ar x libcm.a ; ar x libcmselect.a ;  ar x libcmsockets.a
	cd /tmp/cm_build ; rm libcm.a libcmselect.a libcmsockets.a
	cd /tmp/cm_build ; ld -r *.o -o $@
	cp /tmp/cm_build/cm_module.o .
	rm -fr /tmp/cm_build 

env.c: kcm.h
	rm -f env.c
	$(LN_S) kernel/env.c env.c

presym.c: kcm.h
	rm -f presym.c
	$(LN_S) kernel/presym.c presym.c

kcm.c: kcm.h
	rm -f kcm.c
	$(LN_S) kernel/kcm.c kcm.c

kcm.h:
	rm -f kcm.h
	$(LN_S) kernel/kcm.h kcm.h

