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

extern int vfprintf();


static int trace_val[CMLastTraceType] = {-1, -1, -1, -1, -1, -1, -1};

extern void CMprint_version();

extern int
CMtrace_on(CManager cm, CMTraceType trace_type)
{
    if (trace_val[0] == -1) {
	int i, trace = 0;
	trace_val[0] = 0;
	trace_val[CMControlVerbose] = (cercs_getenv("CMControlVerbose") != NULL);
	trace_val[CMConnectionVerbose] = (cercs_getenv("CMConnectionVerbose") != NULL);
	trace_val[CMDataVerbose] = (cercs_getenv("CMDataVerbose") != NULL);
	trace_val[CMTransportVerbose] = (cercs_getenv("CMTransportVerbose") != NULL);
	trace_val[CMFormatVerbose] = (cercs_getenv("CMFormatVerbose") != NULL);
	trace_val[CMFreeVerbose] = (cercs_getenv("CMFreeVerbose") != NULL);
	if (cercs_getenv("CMVerbose") != NULL) {
	    int i;
	    for (i=0; i<CMLastTraceType; i++)
		trace_val[i] = 1;
	}
	/* for low level verbose, value overrides general CMVerbose */
	trace_val[CMLowLevelVerbose] = (cercs_getenv("CMLowLevelVerbose") != NULL);
	for (i = 0; i < sizeof(trace_val)/sizeof(trace_val[0]); i++) {
	    trace |= trace_val[i];
	}
	if (trace != 0) {
	    CMprint_version();
	}
    }

    return trace_val[trace_type];
}

extern void
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
	printf("\n");
    }
#endif
}

extern void*
CMrealloc(ptr, size)
void *ptr;
int size;
{
    void *tmp = realloc(ptr, size);
    if ((tmp == 0) && (size != 0)) {
	printf("Realloc failed on ptr %lx, size %d\n", (long)ptr, size);
	perror("realloc");
    }
    return tmp;
}

extern void*
CMmalloc(size)
int size;
{
    return malloc(size);
}

extern void
CMfree(ptr)
void *ptr;
{
    free(ptr);
}

