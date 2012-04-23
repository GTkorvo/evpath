dnl Process this file with autoconf to create configure.

AC_INIT([evpath],[3.0],[eisen@gatech.edu])
AC_CONFIG_SRCDIR([cm.c])
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AM_INIT_AUTOMAKE(cmdemo,0.1)
AC_CONFIG_HEADERS([config.h])
AC_CANONICAL_HOST

AC_PROG_CC
AC_COMPILE_WARNINGS

AM_MAINTAINER_MODE
AC_C_CONST
AC_HAS_STRUCT_HOSTENT
AC_STRUCT_FDS_BITS
AC_TYPE_PID_T
AC_EXEEXT

AC_CHECK_HEADERS(unistd.h malloc.h netdb.h windows.h sys/sockio.h sys/uio.h sys/time.h sys/times.h sys/select.h) 

DEMO_LIBS="-lffs $DEMO_LIBS"
case "$host_os" in
 cygwin* | mingw* | pw32*)
   SOCKET_LIBS="-lwsock32 -luser32 -ladvapi32"
   DEMO_LIBS="-lffs $DEMO_LIBS $SOCKET_LIBS";;
esac

dnl Enable building of the convenience library
dnl and set LIBLTDL accordingly
LT_CONFIG_LTDL_DIR([libltdl])
LT_INIT([dlopen])
LT_WITH_LTDL
LTDL_INIT([convenience])
test -n "$LIBTOOL" && LIBTOOL="$LIBTOOL --silent"
AC_SUBST(LIBTOOL_DEPS)

dnl Configure libltdl
AC_CHECK_PROGS(TAGS, etags ctags, echo)
AC_CHECK_PROGS(MAKEDEPEND, makedepend, :)
AC_PATH_PROGS(PERL, perl5 perl, perl)

AC_CHECK_HEADERS(math.h)
AC_GETDOMAINNAME_DEFINED

checkBoth=0
AC_CHECK_FUNCS(writev)
AC_CHECK_FUNC(connect, checkSocket=0, checkSocket=1)
if test "$checkSocket" = 1; then
    AC_CHECK_LIB(socket, connect, LIBS="$LIBS -lsocket", checkBoth=1)
    if test "$checkBoth" = 1; then
        oldLibs=$LIBS
        LIBS="$LIBS -lsocket -lnsl"
        AC_CHECK_FUNC(accept, checkNsl=0, [LIBS=$oldLibs])
    fi
   AC_CHECK_FUNC(gethostbyname, , AC_CHECK_LIB(nsl, getdomainname, [LIBS="$LIBS -lnsl"]))
fi
AC_CHECK_LIB(m,sqrt,[MATH_LIB=-lm])
AC_SUBST(MATH_LIB)
AC_CHECK_FUNC(inet_aton,,[LIBS="$LIBS -lresolv"])
CERCS_REQUIRE_PACKAGE(ffs, ffs.h, libffs.la)
CERCS_REQUIRE_PACKAGE(dill, , libdill.la)
CERCS_REQUIRE_PACKAGE(gen_thread, gen_thread.h,libgen_thread.la)
CERCS_REQUIRE_PACKAGE(cercs_env, cercs_env.h,libcercs_env.la)
if test -n "$cercs_cv_gen_thread_link_dir"; then
LDFLAGS="$LDFLAGS -L$cercs_cv_gen_thread_link_dir"
fi
if test -n "$cercs_cv_cercs_env_link_dir"; then
LDFLAGS="$LDFLAGS -L$cercs_cv_cercs_env_link_dir"
fi
if test -n "$cercs_cv_ffs_link_arg"; then
FFSLINKARG="$cercs_cv_ffs_link_arg"
fi
AC_SUBST(FFSLINKARG)
evlibdir=`(
                   test "x$prefix" = xNONE && prefix="$ac_default_prefix"
                   test "x$exec_prefix" = xNONE && exec_prefix="${prefix}"
                   eval echo \""$libdir"\"
               )`
test -n "$ac_cv_prog_PATHPROG" && evlibdir=`$ac_cv_prog_PATHPROG $evlibdir | tr '\134' /`
AC_DEFINE_UNQUOTED(EVPATH_LIBRARY_INSTALL_DIR,["$evlibdir"],[Place where evpath transport libraries are installed])

evbuilddir=`pwd`
test -n "$ac_cv_prog_PATHPROG" && evbuilddir=`$ac_cv_prog_PATHPROG $evbuilddir | tr '\134' /`
AC_DEFINE_UNQUOTED(EVPATH_LIBRARY_BUILD_DIR,["$evbuilddir"],[Place where evpath transport libraries are built])
AC_CHECK_HEADERS(gen_thread.h sys/un.h)

CERCS_REQUIRE_PACKAGE(atl,atl.h,libatl.la)
dnl can't use CHECK_LIB on libtool libraries, just add it to LTLIBS
LTLIBS="$cercs_cv_atl_link_dir/libatl.la $LTLIBS"

CERCS_REQUIRE_PACKAGE(enet,enet/enet.h,libenet.la)

CERCS_REQUIRE_PACKAGE(nnti,nnti.h,libtrios_nssi.so)

AC_CHECK_LIB(ibverbs, ibv_open_device, [build_ib=enable], [build_ib=disable])
AC_CHECK_LIB(enet,enet_initialize, [build_enet=enable],[build_enet=disable])
AC_CHECK_LIB(trios_nssi,NNTI_init, [build_nnti=enable],[build_nnti=disable])

computenode=disable
do_dyn_linking=0
AC_ARG_ENABLE(CNL, [  --enable-CNL Enable a restricted socket only transport build to allow building on compute node linux. Default is disabled],
	[ if test "x$enableval" = "xtrue"; then
		echo "Configuring for compute node linux"
		do_dyn_linking=1
		computenode=enable
	else
		do_dyn_linking=0
		computenode=disable
	fi])
if test x$computenode = xenable
   then
   AC_DEFINE([TARGET_CNL],[1],[The target platform is Cray Compute Node Linux (CNL).])
fi
AC_DEFINE_UNQUOTED([NO_DYNAMIC_LINKING], [$do_dyn_linking], [Build with dynamic linking])
AM_CONDITIONAL([CNL], [test x$computenode = xenable])
AM_CONDITIONAL([BUILD_IB], [test x$build_ib = xenable])
AM_CONDITIONAL([BUILD_ENET], [test x$build_enet = xenable])
AM_CONDITIONAL([BUILD_NNTI], [test x$build_nnti = xenable])

AC_SUBST(EVDIR)
AC_SUBST(LTLIBS)
AC_SUBST(DEMO_LIBS)
AC_SUBST(SOCKET_LIBS)

AC_CHECK_LIB(pthread,pthread_create)
if test x$ac_cv_lib_pthread_pthread_create = xyes ; then
AC_CHECK_HEADER(pthread.h,
[DEMO_LIBS="$DEMO_LIBS -lpthread"
AC_DEFINE(USE_PTHREADS,1,[Define this if Pthreads should be used for running
tests])
USE_PTHREADS="yes"
],,)
fi

dnl Attempt to get the right tests in place for sysinfo and sysctl.  
CERCS_CHECK_SYSCTL_STYLE
AC_CHECK_FUNCS([gettimeofday uname sysinfo sysconf getloadavg])

LDFLAGS="$LDFLAGS $LDRFLAGS"
dnl Output the makefile
AC_CONFIG_FILES([Makefile evpath-3.0.pc mtests/Makefile tests/Makefile tests/testdll/Makefile rtests/Makefile examples/Makefile old_tests/Makefile dfg_tests/Makefile libltdl/Makefile])
AC_OUTPUT
