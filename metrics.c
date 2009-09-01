#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
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
#define SAMPLEBUFFERSIZE 8192
#endif

#define NUM_CPUSTATES_24X 4
#define NUM_CPUSTATES_26X 7
static unsigned int num_cpustates;

struct stat struct_stat;
#define CPU_FREQ_SCALING_MAX_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"
#define CPU_FREQ_SCALING_MIN_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq"
#define CPU_FREQ_SCALING_CUR_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"
#define CPU_FREQ_SCALING_AVAILABLE_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies"

#define CPU_FREQ_SCALING_GOVERNOR "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define CPU_FREQ_SCALING_AVAILABLE_GOVERNORS "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors"

#define CPU_FREQ_AFFECTED_CPU "/sys/devices/system/cpu/cpu0/cpufreq/affected_cpus"

#define CPU_FREQ_ONDEMAND_SAMPLING_RATE_MIN "/sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate_min"
#define CPU_FREQ_ONDEMAND_SAMPLING_RATE_MAX "/sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate_max"
#define CPU_FREQ_ONDEMAND_SAMPLING_RATE "/sys/devices/system/cpu/cpu0/cpufreq/ondemand/sampling_rate"
#define CPU_FREQ_ONDEMAND_UP_THRESHOLD "/sys/devices/system/cpu/cpu0/cpufreq/ondemand/up_threshold"
#define CPU_FREQ_ONDEMAND_IGNORE_NICE_LOAD "/sys/devices/system/cpu/cpu0/cpufreq/ondemand/ignore_nice_load"
#define CPU_FREQ_ONDEMAND_POWERSAVE_BIAD "/sys/devices/system/cpu/cpu0/cpufreq/ondemand/powersave_bias"

char sys_devices_system_cpu[32];
char sys_devices_system_cpu_available[128];

typedef struct sensor_slurp {
	char *name;
	char buffer[SAMPLEBUFFERSIZE];
} sensor_slurp, *sensor_slurp_ptr;

typedef struct sensor_rec {
	double timestamp;
	char *attr_name;
	float rec_val;
} sensor_rec, *sensor_rec_ptr;

sensor_slurp proc_stat    = { "/proc/stat" };
sensor_slurp proc_loadavg = { "/proc/loadavg" };
sensor_slurp proc_meminfo = { "/proc/meminfo" };
sensor_slurp proc_net_dev = { "/proc/net/dev" };

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

char *update_file(sensor_slurp *sf) {
    (void) slurpfile(sf->name, sf->buffer, SAMPLEBUFFERSIZE);
    return sf->buffer;
}

unsigned int num_cpustates_func ( void ) {
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

unsigned long total_jiffies_func ( void ) {

   char *p;
   unsigned long user_jiffies, nice_jiffies, system_jiffies, idle_jiffies,
                 wio_jiffies, irq_jiffies, sirq_jiffies;

   p = update_file(&proc_stat);
   p = skip_token(p);
   p = skip_whitespace(p);
   user_jiffies = (unsigned int)strtod( p, &p );
   p = skip_whitespace(p);
   nice_jiffies = (unsigned int)strtod( p, &p ); 
   p = skip_whitespace(p);
   system_jiffies = (unsigned int)strtod( p , &p ); 
   p = skip_whitespace(p);
   idle_jiffies = (unsigned int)strtod( p , &p );

   if(num_cpustates == NUM_CPUSTATES_24X)
       return user_jiffies + nice_jiffies + system_jiffies + idle_jiffies;

   p = skip_whitespace(p);
   wio_jiffies = (unsigned int)strtod( p , &p );
   p = skip_whitespace(p);
   irq_jiffies = (unsigned int)strtod( p , &p );
   p = skip_whitespace(p);
   sirq_jiffies = (unsigned int)strtod( p , &p );
  
   return user_jiffies + nice_jiffies + system_jiffies + idle_jiffies +
          wio_jiffies + irq_jiffies + sirq_jiffies; 
}   

double cpu_user_func ( void )
{
    char *p;
    double val;
    static int first = 1;
    static double last_user_jiffies, last_total_jiffies;
    double user_jiffies, total_jiffies, diff;
   
    p = update_file(&proc_stat);

    p = skip_token(p);
    user_jiffies  = strtod( p , (char **)NULL );
    total_jiffies = (double) total_jiffies_func();

    if (first) {
	last_user_jiffies = user_jiffies;
	last_total_jiffies = total_jiffies;
	first = 0;
	return 0.0;
    }
    diff = user_jiffies - last_user_jiffies; 
    
    if ( diff )
	val = (diff/(total_jiffies - last_total_jiffies))*100;
    else
	val = 0.0;

    last_user_jiffies  = user_jiffies;
    last_total_jiffies = total_jiffies;
    
    return val;
}

double cpu_nice_func ( void )
{
    char *p;
    double val;
    static int first = 1;
    static double last_nice_jiffies, last_total_jiffies;
    double nice_jiffies, total_jiffies, diff;
    
    p = update_file(&proc_stat);
 
    p = skip_token(p);
    p = skip_token(p);
    nice_jiffies  = strtod( p , (char **)NULL );
    total_jiffies = (double) total_jiffies_func();

    if (first) {
	last_nice_jiffies = nice_jiffies;
	last_total_jiffies = total_jiffies;
	first = 0;
	return 0.0;
    }
    diff = (nice_jiffies  - last_nice_jiffies);

    if ( diff )
	val = (diff/(total_jiffies - last_total_jiffies))*100;
    else
	val = 0.0;

    last_nice_jiffies  = nice_jiffies;
    last_total_jiffies = total_jiffies;
    
    return val;
}

double cpu_system_func ( void )
{
    char *p;
    static double val;
    static double last_system_jiffies,  system_jiffies,
	last_total_jiffies, total_jiffies, diff;
 
    p = update_file(&proc_stat);
    p = skip_token(p);
    p = skip_token(p);
    p = skip_token(p);
    system_jiffies = strtod( p , (char **)NULL );
    if (num_cpustates > NUM_CPUSTATES_24X) {
	p = skip_token(p);
	p = skip_token(p);
	p = skip_token(p);
	system_jiffies += strtod( p , (char **)NULL ); /* "intr" counted in system */
	p = skip_token(p);
	system_jiffies += strtod( p , (char **)NULL ); /* "sintr" counted in system */
    }
    total_jiffies  = (double) total_jiffies_func();

    diff = system_jiffies  - last_system_jiffies;

    if ( diff )
	val = (diff/(total_jiffies - last_total_jiffies))*100;
    else
	val = 0.0;

    last_system_jiffies  = system_jiffies;
    last_total_jiffies = total_jiffies;   
	
    return val;
}
 
double cpu_idle_func ( void )
{
    char *p;
    static double val; 
    static double last_idle_jiffies,  idle_jiffies,
	last_total_jiffies, total_jiffies, diff;
									  
    p = update_file(&proc_stat);
		      
    p = skip_token(p);
    p = skip_token(p);
    p = skip_token(p);
    p = skip_token(p);
    idle_jiffies  = strtod( p , (char **)NULL );
    total_jiffies = (double) total_jiffies_func();
		 
    diff = idle_jiffies - last_idle_jiffies;

    if ( diff ) 
	val = (diff/(total_jiffies - last_total_jiffies))*100;
    else 
	val = 0.0; 
    last_idle_jiffies = idle_jiffies;
    last_total_jiffies = total_jiffies;
    return val; 
}

double load_one_func ( void )
{
    double val;
    val = strtod( update_file(&proc_loadavg), (char **)NULL);
    return val;
}

double load_five_func ( void )
{
    char *p;
    double val;

    p = update_file(&proc_loadavg);
    p = skip_token(p);
    val = strtod( p, (char **)NULL);

    return val;
}

double load_fifteen_func ( void )
{
    char *p;
    double val;

    p = update_file(&proc_loadavg);

    p = skip_token(p);
    p = skip_token(p);
    val = strtod( p, (char **)NULL);

    return val;
}

double mem_buffers_func ( void )
{
    char *p;
    double val;

    p = strstr( update_file(&proc_meminfo), "Buffers:" );
    if(p) {
	p = skip_token(p);
	val = atof( p ); 
    } else {
	val = 0;
    }

    return val;
}

double mem_free_func ( void )
{
    char *p;
    double val;

    p = strstr( update_file(&proc_meminfo), "MemFree:" );
    if(p) {
	p = skip_token(p);
	val = atof( p );
    } else {
	val = 0;
    }
    
    return val;
}

double mem_cached_func ( void )
{
    char *p;
    double val;

    p = strstr( update_file(&proc_meminfo), "Cached:");
    if(p) {
	p = skip_token(p);
	val = atof( p );
    } else {
	val = 0;
    }

   return val;
}

double swap_free_func ( void )
{
    char *p;
    double val;
   
    p = strstr( update_file(&proc_meminfo), "SwapFree:" );
    if(p) {
	p = skip_token(p);
	val = atof( p ); 
    } else {
	val = 0;
    }

    return val;
}

double gettimeofday_func( void )
{
    double timestamp;
    struct timeval now;
    gettimeofday(&now, NULL);
    timestamp = now.tv_sec * 1.0e6 + now.tv_usec;
    return timestamp;
}

int cpu_max_freq_func( void )
{
    char *p;
    int val = -1;

    if ( stat(CPU_FREQ_SCALING_MAX_FREQ, &struct_stat) == 0 ) {
	if(slurpfile(CPU_FREQ_SCALING_MAX_FREQ, sys_devices_system_cpu, 32)) {
	    p = sys_devices_system_cpu;
	    val = (strtol( p, (char **)NULL , 10 ) / 1000 );
	}
    }
    return val;
}

int cpu_min_freq_func( void )
{
    char *p;
    int val = -1;

    if ( stat(CPU_FREQ_SCALING_MIN_FREQ, &struct_stat) == 0 ) {
	if(slurpfile(CPU_FREQ_SCALING_MIN_FREQ, sys_devices_system_cpu, 32)) {
	    p = sys_devices_system_cpu;
	    val = (strtol( p, (char **)NULL , 10 ) / 1000 );
	}
    }
    return val;
}

int cpu_cur_freq_func( void )
{
    char *p;
    int val = -1;
    
    if ( stat(CPU_FREQ_SCALING_CUR_FREQ, &struct_stat) == 0 ) {
	if(slurpfile(CPU_FREQ_SCALING_CUR_FREQ, sys_devices_system_cpu, 32)) {
	    p = sys_devices_system_cpu;
	    val = (strtol( p, (char **)NULL , 10 ) / 1000 );
	}
    }
    return val;
}

int *cpu_available_freq_func( void )
{
    char *p;
    static int val[3];

    if ( stat(CPU_FREQ_SCALING_AVAILABLE_FREQ, &struct_stat) == 0 ) {
	if(slurpfile(CPU_FREQ_SCALING_AVAILABLE_FREQ, sys_devices_system_cpu_available, 128)) {
	    p = sys_devices_system_cpu_available;
	    val[0] = (strtol( p, (char **)NULL , 10 ) / 1000 );
	    printf (" CPU_MAX_FREQ %%  :  %d \n", val[0]);
	    p = skip_token(p);
	    val[1] = (strtol( p, (char **)NULL , 10 ) / 1000 );
	    printf (" CPU_MID_FREQ %%  :  %d \n", val[1]);
	    p = skip_token(p);
	    val[2] = (strtol( p, (char **)NULL , 10 ) / 1000 );
	    printf (" CPU_MIN_FREQ %%  :  %d \n", val[2]);
	}
    }
    return val;
}

char *cpu_scaling_governor_func( void )
{
    char *p = NULL;
    if ( stat(CPU_FREQ_SCALING_GOVERNOR, &struct_stat) == 0 ) {
	if(slurpfile(CPU_FREQ_SCALING_GOVERNOR, sys_devices_system_cpu, 32)) {
	    p = sys_devices_system_cpu;
	}
    }
    return p;
}

char **cpu_scaling_available_governors_func( void )
{
    static char *val[3];
    char *p;

    if ( stat(CPU_FREQ_SCALING_AVAILABLE_GOVERNORS, &struct_stat) == 0 ) {
	if(slurpfile(CPU_FREQ_SCALING_AVAILABLE_GOVERNORS, sys_devices_system_cpu_available, 128)) {
	    p = sys_devices_system_cpu_available;
	    printf (" CPU_AVAILABLE 1 2 3 %%  :  %s \n", p);
	    val[0] = strtok(p, " ");
	    printf (" CPU_AVAILABLE 1 %%  :  %s \n", val[0]);
	    val[1] = strtok(NULL, " ");
	    printf (" CPU_AVAILABLE 2 %%  :  %s \n", val[1]);
	    val[2] = strtok(NULL, " ");
	    printf (" CPU_AVAILABLE 3 %%  :  %s \n", val[2]);
	}
    }
    return val;
}


void
add_metrics_routines(stone_type stone, cod_parse_context context)
{
    static char extern_string[] = "\
		char **cpu_scaling_available_governors_func();\n\
		char *cpu_scaling_governor_func();\n\
		int *cpu_available_freq_func();\n\
		int cpu_min_freq_func();\n\
		int cpu_max_freq_func();\n\
		double gettimeofday_func();\n\
		double swap_free_func();\n\
		double mem_cached_func();\n\
		double mem_free_func();\n\
		double mem_buffers_func();\n\
		double load_fifteen_func();\n\
		double load_five_func();\n\
		double load_one_func();\n\
		double cpu_idle_func();\n\
		double cpu_system_func();\n\
		double cpu_nice_func();\n\
		double cpu_user_func();\n\
		long total_jiffies_func();\n\
		int num_cpustates_func();\n";

    static cod_extern_entry externs[] = {
	{"cpu_scaling_available_governors_func", (void *) 0},	// 0
	{"cpu_scaling_governor_func", (void *) 0},		// 1
	{"cpu_available_freq_func", (void *) 0},		// 2
	{"cpu_cur_freq_func", (void *) 0},			// 3
	{"cpu_min_freq_func", (void *) 0},			// 4
	{"cpu_max_freq_func", (void *) 0},			// 5
	{"gettimeofday_func", (void *) 0},			// 6
	{"swap_free_func", (void *) 0},				// 7
	{"mem_cached_func", (void *) 0},			// 8
	{"mem_free_func", (void *) 0},				// 9
	{"mem_buffers_func", (void *) 0},			// 10
	{"load_fifteen_func", (void *) 0},			// 11
	{"load_five_func", (void *) 0},				// 12
	{"load_one_func", (void *) 0},				// 13
	{"cpu_idle_func", (void *) 0},				// 14
	{"cpu_system_func", (void *) 0},			// 15
	{"cpu_nice_func", (void *) 0},				// 16
	{"cpu_user_func", (void *) 0},				// 17
	{"total_jiffies_func", (void *) 0},			// 18
	{"num_cpustates_func", (void*) 0},			// 19
	{(void *) 0, (void *) 0}
    };

    (void) stone;
    /*
     * some compilers think it isn't a static initialization to put this
     * in the structure above, so do it explicitly.
     */
    externs[0].extern_value = (void *) (long) cpu_scaling_available_governors_func;
    externs[1].extern_value = (void *) (long) cpu_scaling_governor_func;
    externs[2].extern_value = (void *) (long) cpu_available_freq_func;
    externs[3].extern_value = (void *) (long) cpu_cur_freq_func;
    externs[4].extern_value = (void *) (long) cpu_min_freq_func;
    externs[5].extern_value = (void *) (long) cpu_max_freq_func;
    externs[6].extern_value = (void *) (long) gettimeofday_func;
    externs[7].extern_value = (void *) (long) swap_free_func;
    externs[8].extern_value = (void *) (long) mem_cached_func;
    externs[9].extern_value = (void *) (long) mem_free_func;
    externs[10].extern_value = (void *) (long) mem_buffers_func;
    externs[11].extern_value = (void *) (long) load_fifteen_func;
    externs[12].extern_value = (void *) (long) load_five_func;
    externs[13].extern_value = (void *) (long) load_one_func;
    externs[14].extern_value = (void *) (long) cpu_idle_func;
    externs[15].extern_value = (void *) (long) cpu_system_func;
    externs[16].extern_value = (void *) (long) cpu_nice_func;
    externs[17].extern_value = (void *) (long) cpu_user_func;
    externs[18].extern_value = (void *) (long) total_jiffies_func;
    externs[19].extern_value = (void *) (long) num_cpustates_func;

    cod_assoc_externs(context, externs);
    cod_parse_for_context(extern_string, context);
}
