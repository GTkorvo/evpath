dnl @synopsis CERCS_CHECK_LINUX_SYSINFO


AC_DEFUN([CERCS_CHECK_LINUX_SYSINFO],[
  AC_CACHE_CHECK([whether sysinfo uses the newest Linux version of the sysinfo struct],[ac_cv_cercs_sysinfo],[
  AC_TRY_COMPILE([
#include <sys/sysinfo.h>
],
[int main() {struct sysinfo z;
long sz = sysinfo(&z);
unsigned int t = z.mem_unit;
return (sz == 0) ? 0 : 1; }],[ac_cv_cercs_sysinfo=yes],[ac_cv_cercs_sysinfo=no]
  ) # end of TRY_COMPILE
]) # end of CACHE_CHECK

  AC_MSG_RESULT([$ac_cv_cercs_sysinfo])
  if test x$ac_cv_cercs_sysinfo = xyes
  then
    AC_DEFINE(HAVE_LINUX_SYSINFO, 1,
       [Define this if sysinfo(2) conforms to the latest Linux standards])
  fi
]) # end of AC_DEFUN of CERCS_CHECK_LINUX_SYSINFO

AC_DEFUN([CERCS_CHECK_SYSCTL_STYLE],[
  AC_CHECK_FUNC([sysctl], [ac_cv_cercs_sysctl=yes],
     [ac_cv_cercs_sysctl=no])
  if test x$ac_cv_cercs_sysctl = xyes
  then
    AC_CHECK_HEADERS([sys/sysctl.h])
    AC_CACHE_CHECK([does sysctl use Mac OS X style conventions?], 
      [ac_cv_cercs_mac_sysctl], [
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
#include <sys/sysctl.h>
]],[[
  long val = 0;
  static int mib[2] = {CTL_HW, HW_CPU_FREQ};
  size_t vlen = sizeof(long);
  sysctl(mib,2,&val,&vlen,NULL,0);
  ]]                   )], 
       [ac_cv_cercs_mac_sysctl=yes], [ac_cv_cercs_mac_sysctl=no]) ]
    )
  fi
  if test x$ac_cv_cercs_mac_sysctl = xyes
  then
    AC_DEFINE([HAVE_MAC_SYSCTL], 1, 
                     [Uses the Mac OS X style conventions for sysctl])
  fi
]) #end of AC_DEFUN for CERCS_CHECK_SYSCTL_STYLE

# AC_EGREP_HEADER([VM_SWAPUSAGE], sysctl.h, [ac_cv_cercs_mac_sysctl=yes], [ac_cv_cercs_mac_sysctl=no])

