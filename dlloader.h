#include "dlloader.h"
extern void CMdladdsarchdir(char *dir);
extern void* CMdlopen(char *library, int mode);
extern void* CMdlsym(void *handle, char *symbol);
