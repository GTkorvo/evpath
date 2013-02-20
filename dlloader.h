#ifdef LT_LIBPREFIX
#include "ltdl.h"
#define MODULE_EXT ".la"
#else
#include <dlfcn.h>
#define lt_dlopen(x) CMdlopen(x, 0)
#define lt_dladdsearchdir(x) CMdladdsearchdir(x)
#define lt_dlsym(x, y) CMdlsym(x, y)
#define lt_dlhandle void*
#define lt_dlinit() 0
#define lt_dlerror()  ""
#define MODULE_EXT CMAKE_SHARED_MODULE_SUFFIX
#endif
extern void CMdladdsearchdir(char *dir);
extern void* CMdlopen(char *library, int mode);
extern void* CMdlsym(void *handle, char *symbol);
