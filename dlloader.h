#include <dlfcn.h>
#define lt_dlopen(x) CMdlopen(x, 0)
#define lt_dladdsearchdir(x) CMdladdsearchdir(x)
#define lt_dlsym(x, y) CMdlsym(x, y)
#define lt_dlhandle void*
#define MODULE_EXT CMAKE_SHARED_MODULE_SUFFIX
extern void CMdladdsearchdir(char *dir);
extern void* CMdlopen(char *library, int mode);
extern void* CMdlsym(void *handle, char *symbol);
extern void CMset_dlopen_verbose(int verbose);
