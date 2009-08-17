#include "config.h"
#include "atl.h"
#include "gen_thread.h"
#include "evpath.h"
#include "cm_internal.h"
#include "cercs_env.h"
#ifndef MODULE

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdio.h>
#ifdef STDC_HEADERS
#include <stdarg.h>
#else
#include <varargs.h>
#endif
#include <errno.h>
#else
#include "kernel/kcm.h"
#include "kernel/cm_kernel.h"
#include "kernel/library.h"
#endif

int CMtrace_val[CMLastTraceType] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

extern void EVprint_version();

extern int CMtrace_init(CMTraceType trace_type)
{
    int i, trace = 0;
    char *str;
    CMtrace_val[0] = 0;
    CMtrace_val[EVWarning] = 1;  /* default on */
    CMtrace_val[CMControlVerbose] = (cercs_getenv("CMControlVerbose") != NULL);
    CMtrace_val[CMConnectionVerbose] = (cercs_getenv("CMConnectionVerbose") != NULL);
    CMtrace_val[CMDataVerbose] = (cercs_getenv("CMDataVerbose") != NULL);
    CMtrace_val[CMTransportVerbose] = (cercs_getenv("CMTransportVerbose") != NULL);
    CMtrace_val[CMFormatVerbose] = (cercs_getenv("CMFormatVerbose") != NULL);
    CMtrace_val[CMFreeVerbose] = (cercs_getenv("CMFreeVerbose") != NULL);
    CMtrace_val[CMAttrVerbose] = (cercs_getenv("CMAttrVerbose") != NULL);
    CMtrace_val[EVerbose] = (cercs_getenv("EVerbose") != NULL);
    if ((str = cercs_getenv("EVWarning")) != NULL) {
	sscanf(str, "%d", &CMtrace_val[EVWarning]);
    }
    if (cercs_getenv("CMVerbose") != NULL) {
	int j;
	for (j=0; j<CMLastTraceType; j++)
	    CMtrace_val[j] = 1;
    }
    /* for low level verbose, value overrides general CMVerbose */
    CMtrace_val[CMLowLevelVerbose] = (cercs_getenv("CMLowLevelVerbose") != NULL);
    for (i = 0; i < sizeof(CMtrace_val)/sizeof(CMtrace_val[0]); i++) {
	if (i!=EVWarning) trace |= CMtrace_val[i];
    }
    if (trace != 0) {
	EVprint_version();
    }
    return CMtrace_val[trace_type];
}

/*extern int
CMtrace_on(CManager cm, CMTraceType trace_type)
{
    if (CMtrace_val[0] == -1) {
	CMtrace_init();
    }

    return CMtrace_val[trace_type];
    }*/

 /*extern void
CMtrace_out(CManager cm, CMTraceType trace_type, char *format, ...)
{
#ifndef MODULE
    va_list ap;

    if (CMtrace_on(cm, trace_type)) {
	if (CMtrace_on(cm, CMLowLevelVerbose)) {
	    printf("P%lxT%lx - ", (long) getpid(), (long)thr_thread_self());
	}
#ifdef STDC_HEADERS
	va_start(ap, format);
#else
	va_start(ap);
#endif
	vfprintf(stdout, format, ap);
	va_end(ap);
	printf("\n");
    }
#endif
}
 */
extern void
CMtransport_trace(CManager cm, char *format, ...)
{
#ifndef MODULE
    va_list ap;
    if (CMtrace_on(cm, CMTransportVerbose)) {
#ifdef STDC_HEADERS
	va_start(ap, format);
#else
	va_start(ap);
#endif
	vfprintf(stdout, format, ap);
	va_end(ap);
	(void)cm;
	printf("\n");
    }
#endif
}

extern attr_list 
CMint_create_attr_list(CManager cm, char *file, int line)
{
    attr_list list = create_attr_list();
    (void)cm;
    CMtrace_out(cm, CMAttrVerbose, "Creating attr list %lx at %s:%d", 
		(long)list, file, line);
    return list;
}

extern void 
CMint_free_attr_list(CManager cm, attr_list l, char *file, int line)
{
    int count = attr_list_ref_count(l);
    (void)cm;
    CMtrace_out(cm, CMAttrVerbose, "Freeing attr list %lx at %s:%d, ref count was %d", 
		(long)l, file, line, count);
    free_attr_list(l);
}


extern attr_list 
CMint_add_ref_attr_list(CManager cm, attr_list l, char *file, int line)
{
    int count;
    (void)cm;
    if (l == NULL) return NULL;
    count = attr_list_ref_count(l);
    CMtrace_out(cm, CMAttrVerbose, "Adding ref attr list %lx at %s:%d, ref count now %d", 
		(long)l, file, line, count+1);
    return add_ref_attr_list(l);
}

extern attr_list 
CMint_attr_copy_list(CManager cm, attr_list l, char *file, int line)
{
    attr_list ret = attr_copy_list(l);
    (void)cm;
    CMtrace_out(cm, CMAttrVerbose, "Copy attr list %lx at %s:%d, new list %p", 
		(long)l, file, line, ret);
    return ret;
}

extern void
CMint_attr_merge_lists(CManager cm, attr_list l1, attr_list l2, 
		       char *file, int line)
{
    (void)cm;
    (void)file;
    (void)line;
    attr_merge_lists(l1, l2);
}

extern attr_list 
CMint_decode_attr_from_xmit(CManager cm, void * buf, char *file, int line)
{
    attr_list l = decode_attr_from_xmit(buf);
    (void)cm;
    CMtrace_out(cm, CMAttrVerbose, "decode attr list from xmit at %s:%d, new list %lx", 
		file, line, (long)l);
    return l;
}

extern void*
INT_CMrealloc(void *ptr, int size)
{
    void *tmp = realloc(ptr, size);
    if ((tmp == 0) && (size != 0)) {
	printf("Realloc failed on ptr %lx, size %d\n", (long)ptr, size);
	perror("realloc");
    }
    return tmp;
}

extern void*
INT_CMmalloc(int size)
{
    return malloc(size);
}

extern void
INT_CMfree(void *ptr)
{
    free(ptr);
}

