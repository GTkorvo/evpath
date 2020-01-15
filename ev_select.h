#ifndef EVPATH_SELECT_H
#define EVPATH_SELECT_H

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if defined(__has_feature) && __has_feature(memory_sanitizer)

#include <string.h>
#define EVPATH_FD_ZERO(set) \
do { \
    memset((set), 0, sizeof(fd_set)); \
} while (0)

#else

#define EVPATH_FD_ZERO FD_ZERO

#endif

#endif
