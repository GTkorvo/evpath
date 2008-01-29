## Process this file with automake to produce Makefile.in

AUTOMAKE_OPTIONS = foreign

SUBDIRS = libltdl . mtests tests rtests otests

INCLUDES = $(INCLTDL)

EXTRA_DIST = 

lib_LTLIBRARIES = libevpath.la libcmsockets.la libcmselect.la libcmmulticast.la libcmudp.la
EXTRA_LTLIBRARIES = 

LIBS=@LIBS@
LTLIBS=@LTLIBS@

libevpath_la_SOURCES = cm.c cm_control.c cm_formats.c cm_util.c cm_transport.c\
		       cm_lock.c cm_pbio.c cm_interface.c version.c \
		       cm_threadio.c cm_evol.c evp.c response.c revp.c \
		       evp_compat.c thin_server.c 

libevpath_la_LDFLAGS = $(MATH_LIB) -version-info 1:0:0
libevpath_la_LIBADD =  $(LIBLTDL) -lgen_thread -lcercs_env $(LTLIBS) -lffs -lcod @SOCKET_LIBS@ 
libevpath_la_DEPENDENCIES = $(LIBLTDL) $(LTLIBS)

include_HEADERS = evpath.h evpath_old.h revpath.h

AM_CPPFLAGS =

noinst_HEADERS = assert.h cm_internal.h  ev_internal.h

cm_interface.c revp.c revpath.h: evpath.h gen_interface.pl
	@PERL@ ./gen_interface.pl

libcmsockets_la_SOURCES = cmsockets.c
libcmsockets_la_LIBADD = $(LTLIBS) $(LIBS) @SOCKET_LIBS@ -lcercs_env
libcmsockets_la_LDFLAGS = -module -export-symbols-regex "libcmsockets.*"

libcmselect_la_SOURCES = cmselect.c
libcmselect_la_LIBADD = $(LTLIBS) $(LIBS) @SOCKET_LIBS@
libcmselect_la_LDFLAGS = -module -export-symbols-regex "libcmselect.*" -lgen_thread

libcmmulticast_la_SOURCES = cmmulticast.c
libcmmulticast_la_LIBADD = $(LTLIBS) $(LIBS)
libcmmulticast_la_LDFLAGS = -module -export-symbols-regex "libcmmulticast.*"

libcmudp_la_SOURCES = cmudp.c
libcmudp_la_LIBADD = $(LTLIBS) $(LIBS)
libcmudp_la_LDFLAGS = -module -export-symbols-regex "libcmudp.*"

bin_PROGRAMS =
all-local:
check-local:
install-exec-local:
noinst_PROGRAMS = 
MOSTLYCLEANFILES = cm_interface.c revp.c revpath.h

libtool: $(LIBTOOL_DEPS)
	$(SHELL) ./config.status --recheck

