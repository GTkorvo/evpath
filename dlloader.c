#include "config.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dlloader.h"

static char **search_list = NULL;

void
CMdladdsearchdir(char *string)
{
    int count = 0;
    if (search_list == NULL) {
	search_list = malloc(2*sizeof(char*));
    } else {
	while(search_list[count] != NULL) count++;
	search_list = realloc(search_list, (count+2)*sizeof(char*));
    }
    search_list[count] = strdup(string);
    search_list[count+1] = NULL;
}

typedef struct {
    void *dlopen_handle;
    char *lib_prefix;
} *dlhandle;

void *
CMdlopen(char *in_lib, int mode)
{
#if NO_DYNAMIC_LINKING
    return NULL;
#else
    int i;
    dlhandle dlh;
    void *handle;
    char *tmp;
    char *lib;
    tmp = rindex(in_lib, '.');
    if (tmp && (strcmp(tmp, ".la") == 0)) {
	/* can't open .la files */
	lib = malloc(strlen(in_lib) + strlen(MODULE_EXT) + 8);
	strcpy(lib, in_lib);
	strcpy(rindex(lib, '.'), MODULE_EXT);
    } else {
	lib = strdup(in_lib);
    }
    char **list = search_list;
    while(list && (list[0] != NULL)) {
        char *tmp = malloc(strlen(list[0]) + strlen(lib) + 2);
	sprintf(tmp, "%s/%s", list[0], lib);
	handle = dlopen(tmp, RTLD_LAZY);
	char *err = dlerror();
	list++;
	if (handle) list = NULL; // fall out
    }
    if (!handle) {
        handle = dlopen(lib, RTLD_LAZY);
    }
    if (!handle) return NULL;
    dlh = malloc(sizeof(*dlh));
    tmp = rindex(lib, '/'); /* find name start */
    if (!tmp) tmp = lib;
    dlh->lib_prefix = malloc(strlen(tmp) + 4);
    strcpy(dlh->lib_prefix, tmp);
    tmp = rindex(dlh->lib_prefix, '.');
    strcpy(tmp, "_LTX_");  /* kill postfix, add _LTX_ */
    dlh->dlopen_handle = handle;
    free(lib);
    return (void*)dlh;
#endif
}

void*
CMdlsym(void *vdlh, char *sym)
{
#if NO_DYNAMIC_LINKING
    return NULL;
#else
    dlhandle dlh = (dlhandle)vdlh;
    char *tmp = malloc(strlen(sym) + strlen(dlh->lib_prefix) + 1);
    void *sym_val;
    strcpy(tmp, dlh->lib_prefix);
    strcat(tmp, sym);
    sym_val = dlsym(dlh->dlopen_handle, tmp);
    free(tmp);
    if (!sym_val) 
	sym_val = dlsym(dlh->dlopen_handle, sym);
    return sym_val;
#endif
}
