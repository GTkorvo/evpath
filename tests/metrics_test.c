#include "config.h"

#include <stdio.h>
#include <atl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include "evpath.h"
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define drand48() (((double)rand())/((double)RAND_MAX))
#define lrand48() rand()
#define srand48(x)
#else
#include <sys/wait.h>
#endif

typedef struct _metrics_rec {
    double    dtimeofday;
    int           hw_cpus;
    long           hw_cpu_min_freq;
    long           hw_cpu_max_freq;
    long           hw_cpu_curr_freq;
    char*       os_type;
    char*       os_release;
    char*       hostname;
    double    stat_uptime;
    double    stat_loadavg_one;
    double    stat_loadavg_five;
    double    stat_loadavg_fifteen;
    unsigned long    vm_mem_total;
    unsigned long    vm_mem_free;
    unsigned long    vm_swap_total;
    unsigned long    vm_swap_free;
} metrics_rec, *metrics_rec_ptr;

static FMField metrics_field_list[] =
{
    {"dtimeofday", "double", sizeof(double),
     FMOffset(metrics_rec_ptr, dtimeofday)},
    {"hw_cpus", "integer", sizeof(int),
     FMOffset(metrics_rec_ptr, hw_cpus)},
    {"hw_cpu_min_freq", "integer", sizeof(long),
     FMOffset(metrics_rec_ptr, hw_cpu_min_freq)},
    {"hw_cpu_max_freq", "integer", sizeof(long),
     FMOffset(metrics_rec_ptr, hw_cpu_max_freq)},
    {"hw_cpu_curr_freq", "integer", sizeof(long),
     FMOffset(metrics_rec_ptr, hw_cpu_curr_freq)},
    {"os_type", "string", sizeof(char*),
     FMOffset(metrics_rec_ptr, os_type)},
    {"os_release", "string", sizeof(char*),
     FMOffset(metrics_rec_ptr, os_release)},
    {"hostname", "string", sizeof(char*),
     FMOffset(metrics_rec_ptr, hostname)},
    {"stat_uptime", "double", sizeof(double),
     FMOffset(metrics_rec_ptr, stat_uptime)},
    {"stat_loadavg_one", "double", sizeof(double),
     FMOffset(metrics_rec_ptr, stat_loadavg_one)},
    {"stat_loadavg_five", "double", sizeof(double),
     FMOffset(metrics_rec_ptr, stat_loadavg_five)},
    {"stat_loadavg_fifteen", "double", sizeof(double),
     FMOffset(metrics_rec_ptr, stat_loadavg_fifteen)},
    {"vm_mem_total", "unsigned integer", sizeof(unsigned long),
     FMOffset(metrics_rec_ptr, vm_mem_total)},
    {"vm_mem_free", "unsigned integer", sizeof(unsigned long),
     FMOffset(metrics_rec_ptr, vm_mem_free)},
    {"vm_swap_total", "unsigned integer", sizeof(unsigned long),
     FMOffset(metrics_rec_ptr, vm_swap_total)},
    {"vm_swap_free", "unsigned integer", sizeof(unsigned long),
     FMOffset(metrics_rec_ptr, vm_swap_free)},
    {NULL, NULL, 0, 0}
};

static FMStructDescRec simple_format_list[] =
{
    {"metrics", metrics_field_list, sizeof(metrics_rec), NULL},
    {NULL, NULL}
};

int quiet = 1;

static
int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    static int first = 1;
    metrics_rec_ptr event = vevent;
    static metrics_rec first_event;
    if (quiet <= 0) {
	printf("In the handler, event data is :\n");
	printf("	dtimeofday = %g\n", event->dtimeofday);
	printf("	hw_cpus = %d\n", event->hw_cpus);
	printf("	hw_cpu_min_freq = %ld\n", event->hw_cpu_min_freq);
	printf("	hw_cpu_max_freq = %ld\n", event->hw_cpu_max_freq);
	printf("	hw_cpu_curr_freq = %ld\n", event->hw_cpu_curr_freq);
	printf("	os_type = %s\n", event->os_type);
	printf("	os_release = %s\n", event->os_release);
	printf("	hostname = %s\n", event->hostname);
	printf("	stat_uptime = %g\n", event->stat_uptime);
	printf("	stat_loadavg_one = %g\n", event->stat_loadavg_one);
	printf("	stat_loadavg_five = %g\n", event->stat_loadavg_five);
	printf("	stat_loadavg_fifteen = %g\n", event->stat_loadavg_fifteen);
	printf("	vm_mem_total = %ld\n", event->vm_mem_total);
	printf("	vm_mem_free = %ld\n", event->vm_mem_free);
	printf("	vm_swap_total = %ld\n", event->vm_swap_total);
	printf("	vm_swap_free = %ld\n", event->vm_swap_free);
    }
    if (first) {
	first = 0;
	first_event = *event;
	return 0;
    }
    (void)cm;
    if (client_data != NULL) {
	int tmp = *((int *) client_data);
	*((int *) client_data) = tmp + 1;
    }
    return 0;
}

static int do_regression_master_test();
static int regression = 1;
static atom_t CM_TRANSPORT;

char *ECL_generate = "{\n\
    static int count = 0;\n\
    output.dtimeofday = dgettimeofday();\n\
    output.hw_cpus = hw_cpus();\n\
    output.hw_cpu_min_freq = hw_cpu_min_freq();\n\
    output.hw_cpu_max_freq = hw_cpu_max_freq();\n\
    output.hw_cpu_curr_freq = hw_cpu_curr_freq();\n\
    output.os_type = os_type();\n\
    output.os_release = os_release();          \n\
    output.hostname = hostname();          \n\
    output.stat_uptime = stat_uptime();           \n\
    output.stat_loadavg_one = stat_loadavg_one();       \n	\
    output.stat_loadavg_five = stat_loadavg_five();      \n	\
    output.stat_loadavg_fifteen = stat_loadavg_fifteen();   \n	\
    output.vm_mem_total = vm_mem_total();   \n	\
    output.vm_mem_free = vm_mem_free();    \n	\
    output.vm_swap_total = vm_swap_total();   \n	\
    output.vm_swap_free = vm_swap_free();     \n\
    count++;\n\
    return count <= 2;\n\
}";

char *transport = NULL;

#include "support.c"

int
main(int argc, char **argv)
{
    CManager cm;
    int regression_master = 1;

    PARSE_ARGS();

    srand48(getpid());
    CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");

    if (regression && regression_master) {
	return do_regression_master_test();
    }
    cm = CManager_create();
/*    (void) CMfork_comm_thread(cm);*/

    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	char *postfix = NULL;
	char *string_list;
	EVstone stone;
	if (!transport) transport = getenv("CMTransport");
	if (transport != NULL) {
	    if (listen_list == NULL) listen_list = create_attr_list();
	    add_attr(listen_list, CM_TRANSPORT, Attr_String,
		     (attr_value) strdup(transport));
	}
	CMlisten_specific(cm, listen_list);
	contact_list = CMget_contact_list(cm);
	string_list = attr_list_to_string(contact_list);

	stone = EValloc_stone(cm);
	EVassoc_terminal_action(cm, stone, simple_format_list, simple_handler, NULL);
	printf("Contact list \"%d:%s\"\n", stone, string_list);
	CMsleep(cm, 120);
    } else {
	attr_list attrs;
	int remote_stone, stone = 0;
	char *action_spec = create_transform_action_spec(NULL,simple_format_list,ECL_generate);
	EVstone auto_stone;
	EVaction auto_action;
	atom_t CMDEMO_TEST_ATOM;
	if (argc == 2) {
	    attr_list contact_list;
	    char *list_str;
	    sscanf(argv[1], "%d:", &remote_stone);
	    list_str = strchr(argv[1], ':') + 1;
	    contact_list = attr_list_from_string(list_str);
	    stone = EValloc_stone(cm);
	    EVassoc_bridge_action(cm, stone, contact_list, remote_stone);
	    free_attr_list(contact_list);
	}
	auto_stone = EValloc_stone (cm);
	auto_action = EVassoc_immediate_action (cm, auto_stone, action_spec, 0);
	free(action_spec);
	EVaction_set_output(cm, auto_stone, auto_action, 0, stone);
	EVenable_auto_stone(cm, auto_stone, 1, 0);
	attrs = create_attr_list();
	CMDEMO_TEST_ATOM = attr_atom_from_string("CMdemo_test_atom");
	add_attr(attrs, CMDEMO_TEST_ATOM, Attr_Int4, (attr_value)45678);
	CMsleep(cm, 3);
	free_attr_list(attrs);
    }
    CManager_close(cm);
    return 0;
}

static pid_t subproc_proc = 0;

static void
fail_and_die(int signal)
{
    (void)signal;
    fprintf(stderr, "EVtest failed to complete in reasonable time\n");
    if (subproc_proc != 0) {
	kill(subproc_proc, 9);
    }
    exit(1);
}

static int
do_regression_master_test()
{
    CManager cm;
    char *args[] = {"evtest", "-c", NULL, NULL};
    int exit_state;
    int forked = 0;
    attr_list contact_list, listen_list = NULL;
    char *string_list, *transport, *postfix;
    int message_count = 0, i;
    EVstone handle;
#ifdef HAVE_WINDOWS_H
    SetTimer(NULL, 5, 1000, (TIMERPROC) fail_and_die);
#else
    struct sigaction sigact;
    sigact.sa_flags = 0;
    sigact.sa_handler = fail_and_die;
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGALRM);
    sigaction(SIGALRM, &sigact, NULL);
    alarm(300);
#endif
    cm = CManager_create();
    forked = CMfork_comm_thread(cm);
    if ((transport = getenv("CMTransport")) != NULL) {
	listen_list = create_attr_list();
	add_attr(listen_list, CM_TRANSPORT, Attr_String,
		 (attr_value) strdup(transport));
    }
    CMlisten_specific(cm, listen_list);
    contact_list = CMget_contact_list(cm);
    string_list = attr_list_to_string(contact_list);
    free_attr_list(contact_list);

    if (quiet <= 0) {
	if (forked) {
	    printf("Forked a communication thread\n");
	} else {
	    printf("Doing non-threaded communication handling\n");
	}
    }
    srand48(1);

    handle = EValloc_stone(cm);
    EVassoc_terminal_action(cm, handle, simple_format_list, simple_handler, &message_count);
    
    args[2] = string_list;
    args[2] = malloc(10 + strlen(string_list));
    sprintf(args[2], "%d:%s", handle, string_list);
    subproc_proc = run_subprocess(args);

    /* give him time to start */
    for (i=0; i< 10; i++) {
	if (message_count == 1) break;
	CMsleep(cm, 1);
    }
/* stuff */
    if (quiet <= 0) {
	printf("Waiting for remote....\n");
    }
#ifdef HAVE_WINDOWS_H
    if (_cwait(&exit_state, subproc_proc, 0) == -1) {
	perror("cwait");
    }
    if (exit_state == 0) {
	if (quiet <= 0) 
	    printf("Passed single remote subproc test\n");
    } else {
	printf("Single remote subproc exit with status %d\n",
	       exit_state);
    }
#else
    if (waitpid(subproc_proc, &exit_state, 0) == -1) {
	perror("waitpid");
    }
    if (WIFEXITED(exit_state)) {
	if (WEXITSTATUS(exit_state) == 0) {
	    if (quiet <- 1) 
		printf("Passed single remote subproc test\n");
	} else {
	    printf("Single remote subproc exit with status %d\n",
		   WEXITSTATUS(exit_state));
	}
    } else if (WIFSIGNALED(exit_state)) {
	printf("Single remote subproc died with signal %d\n",
	       WTERMSIG(exit_state));
    }
#endif
    free(string_list);
    free(args[2]);
    EVfree_stone(cm, handle);
    CManager_close(cm);
    if (message_count != 1) printf("Message count == %d\n", message_count);
    return !(message_count == 1);
}
