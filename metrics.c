#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "evpath.h"
#include "cm_internal.h"
#include "cod.h"
/* #include <sys/time.h> */
/* #include <sys/types.h> */
/* #include <sys/stat.h> */
/* #include <sys/time.h> */
#include <fcntl.h>
/* #include <ctype.h> */

#ifndef SAMPLEBUFFERSIZE
#define SAMPLEBUFFERSIZE 1024 
#endif

#define NUM_CPUSTATES_24X 4
#define NUM_CPUSTATES_26X 7
static unsigned int num_cpustates;

typedef struct rec_slurp {
	char *name;
	char buffer[SAMPLEBUFFERSIZE];
} rec_slurp, *rec_slurp_ptr;

typedef struct recs {
	double timestamp;
	char *attr_name;
	float rec_val;
} recs, *recs_ptr;

rec_slurp proc_stat    = { "/proc/stat" };
rec_slurp proc_loadavg = { "/proc/loadavg" };
rec_slurp proc_meminfo = { "/proc/meminfo" };
rec_slurp proc_net_dev = { "/proc/net/dev" };

char * skip_whitespace (const char *p)
{
    while (isspace((unsigned char)*p)) p++;
    return (char *)p;
}
 
char * skip_token (const char *p)
{
    while (isspace((unsigned char)*p)) p++;
    while (*p && !isspace((unsigned char)*p)) p++;
    return (char *)p;
}         

int slurpfile(char *filename, char *buffer, int buflen)
{
    int fd, read_len;

    fd = open(filename, O_RDONLY);
    if(fd < 0) {
	printf("open() error on file %s \n", filename); 
	exit(0);
    }
    read_len = read(fd, buffer, buflen);
    if(read_len <= 0) {
	printf("read() error on file %s \n", filename); 
	exit(0);
    }
    if (read_len == buflen) {
	--read_len;
	printf("slurpfile() read() buffer overflow on file %s", filename);
    }
    buffer[read_len] = '\0';
    close(fd);
    return read_len;
}

char *update_file(rec_slurp *rf)
{
    (void) slurpfile(rf->name, rf->buffer, SAMPLEBUFFERSIZE);
    return rf->buffer;
}

unsigned int num_cpustates_func ( void )
{
    char *p;
    unsigned int i=0;

    p = update_file(&proc_stat);

/*
** Skip initial "cpu" token
*/
    p = skip_token(p);
    p = skip_whitespace(p);
/*
** Loop over file until next "cpu" token is found.
** i=4 : Linux 2.4.x
** i=7 : Linux 2.6.x
*/
    while (strncmp(p,"cpu",3)) {
	p = skip_token(p);
	p = skip_whitespace(p);
	i++;
    }
    
    return i;
}

unsigned long total_jiffies_func ( void )
{
   char *p;
   unsigned long user_jiffies, nice_jiffies, system_jiffies, idle_jiffies,
       wio_jiffies, irq_jiffies, sirq_jiffies;

   p = update_file(&proc_stat);
   p = skip_token(p);
   p = skip_whitespace(p);
   user_jiffies = strtod( p, &p );
   p = skip_whitespace(p);
   nice_jiffies = strtod( p, &p ); 
   p = skip_whitespace(p);
   system_jiffies = strtod( p , &p ); 
   p = skip_whitespace(p);
   idle_jiffies = strtod( p , &p );
   
   if(num_cpustates == NUM_CPUSTATES_24X)
       return user_jiffies + nice_jiffies + system_jiffies + idle_jiffies;

   p = skip_whitespace(p);
   wio_jiffies = strtod( p , &p );
   p = skip_whitespace(p);
   irq_jiffies = strtod( p , &p );
   p = skip_whitespace(p);
   sirq_jiffies = strtod( p , &p );
   
   return user_jiffies + nice_jiffies + system_jiffies + idle_jiffies +
       wio_jiffies + irq_jiffies + sirq_jiffies; 
}   

 
float cpu_idle_func ( void )
{
    char *p;
    static float val; 
    static double last_idle_jiffies,  idle_jiffies,
	last_total_jiffies, total_jiffies, diff;
									  
    p = update_file(&proc_stat);
		      
    p = skip_token(p);
    p = skip_token(p);
    p = skip_token(p);
    p = skip_token(p);
    idle_jiffies  = strtod( p , (char **)NULL );
    total_jiffies = total_jiffies_func();
    
    diff = idle_jiffies - last_idle_jiffies;

    if ( diff ) 
	val = (diff/(total_jiffies - last_total_jiffies))*100;
    else 
	val = 0.0; 
    last_idle_jiffies = idle_jiffies;
    last_total_jiffies = total_jiffies;
    return val; 
}

void
add_metrics_routines(stone, context)
stone_type stone;
cod_parse_context context;
{
    static char extern_string[] = "\
		float cpu_idle_func();\n\
		int num_cpustates_func();\n";

    static cod_extern_entry externs[] = {
	{"cpu_idle_func", (void *) 0},
	{"num_cpustates_func", (void*) 0},
	{(void *) 0, (void *) 0}
    };

    /*
     * some compilers think it isn't a static initialization to put this
     * in the structure above, so do it explicitly.
     */
    externs[0].extern_value = (void *) (long) cpu_idle_func;
    externs[1].extern_value = (void *) (long) num_cpustates_func;

    cod_assoc_externs(context, externs);
    cod_parse_for_context(extern_string, context);
}
