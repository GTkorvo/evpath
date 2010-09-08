#if defined (__INTEL_COMPILER)
#pragma warning (disable:981)
#endif
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
#include "evpath_old.h"
#include "gen_thread.h"
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define drand48() (((double)rand())/((double)RAND_MAX))
#define lrand48() rand()
#define srand48(x)
#else
#include <sys/wait.h>
#endif

typedef struct _complex_rec {
    double r;
    double i;
} complex, *complex_ptr;

typedef struct _nested_rec {
    complex item;
} nested, *nested_ptr;

static IOField nested_field_list[] =
{
    {"item", "complex", sizeof(complex), IOOffset(nested_ptr, item)},
    {NULL, NULL, 0, 0}
};

static IOField complex_field_list[] =
{
    {"r", "double", sizeof(double), IOOffset(complex_ptr, r)},
    {"i", "double", sizeof(double), IOOffset(complex_ptr, i)},
    {NULL, NULL, 0, 0}
};

typedef struct _simple_rec {
    int integer_field;
    short short_field;
    long long_field;
    nested nested_field;
    double double_field;
    char char_field;
    int scan_sum;
} simple_rec, *simple_rec_ptr;

typedef struct _output_rec {
    int random_field;
    int integer_field;
    short sum1_field;
    long sum2_field;
    nested nested_field;
    double double_field;
    char char_field;
    int sum3_field;
} output_rec, *output_rec_ptr;

static IOField simple_field_list[] =
{
    {"integer_field", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, integer_field)},
    {"short_field", "integer",
     sizeof(short), IOOffset(simple_rec_ptr, short_field)},
    {"long_field", "integer",
     sizeof(long), IOOffset(simple_rec_ptr, long_field)},
    {"nested_field", "nested",
     sizeof(nested), IOOffset(simple_rec_ptr, nested_field)},
    {"double_field", "float",
     sizeof(double), IOOffset(simple_rec_ptr, double_field)},
    {"char_field", "char",
     sizeof(char), IOOffset(simple_rec_ptr, char_field)},
    {"scan_sum", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, scan_sum)},
    {NULL, NULL, 0, 0}
};

static IOField output_field_list[] =
{
    {"random_field", "integer",
     sizeof(int), IOOffset(output_rec_ptr, random_field)},
    {"integer_field", "integer",
     sizeof(int), IOOffset(output_rec_ptr, integer_field)},
    {"sum1_field", "integer",
     sizeof(short), IOOffset(output_rec_ptr, sum1_field)},
    {"sum2_field", "integer",
     sizeof(long), IOOffset(output_rec_ptr, sum2_field)},
    {"nested_field", "nested",
     sizeof(nested), IOOffset(output_rec_ptr, nested_field)},
    {"double_field", "float",
     sizeof(double), IOOffset(output_rec_ptr, double_field)},
    {"char_field", "char",
     sizeof(char), IOOffset(output_rec_ptr, char_field)},
    {"sum3_field", "integer",
     sizeof(int), IOOffset(output_rec_ptr, sum3_field)},
    {NULL, NULL, 0, 0}
};

static CMFormatRec simple_format_list[] =
{
    {"simple", simple_field_list},
    {"complex", complex_field_list},
    {"nested", nested_field_list},
    {NULL, NULL}
};

static CMFormatRec output_format_list[] =
{
    {"output_struct", output_field_list},
    {"complex", complex_field_list},
    {"nested", nested_field_list},
    {NULL, NULL}
};

static
void 
generate_record(simple_rec_ptr event)
{
    long sum = 0;
    event->integer_field = (int) lrand48() % 100;
    sum += event->integer_field % 100;
    event->short_field = ((short) lrand48());
    sum += event->short_field % 100;
    event->long_field = ((long) lrand48());
    sum += event->long_field % 100;

    event->nested_field.item.r = drand48();
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    event->nested_field.item.i = drand48();
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;

    event->double_field = drand48();
    sum += ((int) (event->double_field * 100.0)) % 100;
    event->char_field = lrand48() % 128;
    sum += event->char_field;
    sum = sum % 100;
    event->scan_sum = (int) sum;
}

int quiet = 1;

static
int
output_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    output_rec_ptr event = vevent;
    long sum  =  event->random_field, scan_sum = 0;
    (void)cm;
    sum += event->integer_field % 100;
    sum += (event->sum1_field % 100);
    sum += event->sum2_field;
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;
    sum += ((int) (event->double_field * 100.0)) % 100;
    sum += event->char_field;
    sum = sum % 100;
    scan_sum = event->sum3_field;
    if (sum != scan_sum) {
	printf("Received record checksum does not match. expected %d, got %d\n",
	       (int) sum, (int) scan_sum);
    }
    if ((quiet <= 0) || (sum != scan_sum)) {
	printf("In the handler, event data is :\n");
	printf("	random_field = %d\n", event->random_field);
	printf("	integer_field = %d\n", event->integer_field);
	printf("	sum1_field = %d\n", event->sum1_field);
	printf("	sum2_field = %ld\n", event->sum2_field);
	printf("	double_field = %g\n", event->double_field);
	printf("	char_field = %c\n", event->char_field);
	printf("Data was received with attributes : \n");
	if (attrs) dump_attr_list(attrs);
    }
    if (client_data != NULL) {
	int tmp = *((int *) client_data);
	*((int *) client_data) = tmp + 1;
    }
    return 0;
}

static int do_regression_master_test();
static int regression = 1;
static int repeat_count = 10;

static char *trans = "{\
    output.random_field = (lrand48() % 10);\n\
    output.integer_field = input.integer_field;\n\
    output.sum1_field = input.short_field;\n\
    output.sum2_field = input.long_field - output.random_field;\n\
    output.nested_field.item.i = input.nested_field.item.i;\n\
    output.nested_field.item.r = input.nested_field.item.r;\n\
    output.double_field = input.double_field;\n\
    output.char_field = input.char_field;\n\
    output.sum3_field = input.scan_sum;\n\
    return input.long_field % 2;\n\
}\0\0";

int
main(int argc, char **argv)
{
    CManager cm;
    int regression_master = 1;

    while (argv[1] && (argv[1][0] == '-')) {
	if (argv[1][1] == 'c') {
	    regression_master = 0;
	} else if (argv[1][1] == 's') {
	    regression_master = 0;
	} else if (argv[1][1] == 'q') {
	    quiet++;
	} else if (argv[1][1] == 'v') {
	    quiet--;
	} else if (argv[1][1] == 'n') {
	    regression = 0;
	    quiet = -1;
	}
	argv++;
	argc--;
    }
    srand48(getpid());
#ifdef USE_PTHREADS
    gen_pthread_init();
#endif
    if (regression && regression_master) {
	return do_regression_master_test();
    }
    cm = CManager_create();
/*    (void) CMfork_comm_thread(cm);*/

    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	char *transport = NULL;
	char *postfix = NULL;
	char *string_list;
	char *filter;
	EVstone term, fstone;
	EVaction faction;
	if ((transport = getenv("CMTransport")) != NULL) {
	    if (listen_list == NULL) listen_list = create_attr_list();
	    add_attr(listen_list, CM_TRANSPORT, Attr_String,
		     (attr_value) strdup(transport));
	}
	if ((postfix = getenv("CMNetworkPostfix")) != NULL) {
	    if (listen_list == NULL) listen_list = create_attr_list();
	    add_attr(listen_list, CM_NETWORK_POSTFIX, Attr_String,
		     (attr_value) strdup(postfix));
	}
	CMlisten_specific(cm, listen_list);
	contact_list = CMget_contact_list(cm);
	if (contact_list) {
	    string_list = attr_list_to_string(contact_list);
	} else {
	    /* must be multicast, hardcode a contact list */
#define HELLO_PORT 12345
#define HELLO_GROUP "225.0.0.37"
	    int addr;
	    (void) inet_aton(HELLO_GROUP, (struct in_addr *)&addr);
	    contact_list = create_attr_list();
	    add_attr(contact_list, CM_MCAST_ADDR, Attr_Int4,
		     (attr_value) (long)addr);
	    add_attr(contact_list, CM_MCAST_PORT, Attr_Int4,
		     (attr_value) HELLO_PORT);
	    add_attr(contact_list, CM_TRANSPORT, Attr_String,
		     (attr_value) "multicast");
/*	    conn = CMinitiate_conn(cm, contact_list);*/
	    string_list = attr_list_to_string(contact_list);
	    free_attr_list(contact_list);
	}	
	term = EValloc_stone(cm);
	EVassoc_terminal_action(cm, term, output_format_list, output_handler, NULL);
	filter = create_transform_action_spec(simple_format_list, 
					      output_format_list, trans);
	
	fstone = EValloc_stone(cm);
	faction = EVassoc_immediate_action(cm, fstone, filter, NULL);
	EVaction_set_output(cm, fstone, faction, 0, term);
	
	printf("Contact list \"%d:%s\"\n", fstone, string_list);
	CMsleep(cm, 120);
    } else {
	simple_rec data;
	attr_list attrs;
	int remote_stone, stone = 0;
	int count;
	EVsource source_handle;
	atom_t CMDEMO_TEST_ATOM = attr_atom_from_string("CMdemo_test_atom");
	if (argc == 2) {
	    attr_list contact_list;
	    char *list_str;
	    sscanf(argv[1], "%d:", &remote_stone);
	    list_str = strchr(argv[1], ':') + 1;
	    contact_list = attr_list_from_string(list_str);
	    stone = EValloc_stone(cm);
	    EVassoc_output_action(cm, stone, contact_list, remote_stone);
	}
	attrs = create_attr_list();
	add_attr(attrs, CMDEMO_TEST_ATOM, Attr_Int4, (attr_value)45678);
	source_handle = EVcreate_submit_handle(cm, stone, simple_format_list);
	count = repeat_count;
	while (count != 0) {
	    generate_record(&data);
	    if (quiet <=0) {printf("submitting %ld\n", data.long_field);}
	    EVsubmit(source_handle, &data, attrs);
	    if ((data.long_field%2 == 1) && (count != -1)) {
		count--;
	    }
	}
	CMsleep(cm, 1);
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

static
pid_t
run_subprocess(char **args)
{
#ifdef HAVE_WINDOWS_H
    int child;
    child = _spawnv(_P_NOWAIT, "./transform_test.exe", args);
    if (child == -1) {
	printf("failed for transform_test\n");
	perror("spawnv");
    }
    return child;
#else
    pid_t child;
    if (quiet <=0) {printf("Forking subprocess\n");}
    child = fork();
    if (child == 0) {
	/* I'm the child */
	execv("./transform_test", args);
    }
    return child;
#endif
}

static int
do_regression_master_test()
{
    CManager cm;
    char *args[] = {"transform_test", "-c", NULL, NULL};
    char *filter;
    int exit_state;
    int forked = 0;
    attr_list contact_list, listen_list = NULL;
    char *string_list, *transport, *postfix;
    int message_count = 0;
    EVstone term, fstone;
    EVaction faction;
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
    if ((postfix = getenv("CMNetworkPostfix")) != NULL) {
	if (listen_list == NULL) listen_list = create_attr_list();
	add_attr(listen_list, CM_NETWORK_POSTFIX, Attr_String,
		 (attr_value) strdup(postfix));
    }
    CMlisten_specific(cm, listen_list);
    contact_list = CMget_contact_list(cm);
    if (contact_list) {
	string_list = attr_list_to_string(contact_list);
	free_attr_list(contact_list);
    } else {
	/* must be multicast, hardcode a contact list */
#define HELLO_PORT 12345
#define HELLO_GROUP "225.0.0.37"
	int addr;
	(void) inet_aton(HELLO_GROUP, (struct in_addr *)&addr);
	contact_list = create_attr_list();
	add_attr(contact_list, CM_MCAST_ADDR, Attr_Int4,
		 (attr_value) (long)addr);
	add_attr(contact_list, CM_MCAST_PORT, Attr_Int4,
		 (attr_value) HELLO_PORT);
	add_attr(contact_list, CM_TRANSPORT, Attr_String,
		 (attr_value) "multicast");
	(void) CMinitiate_conn(cm, contact_list);
	string_list = attr_list_to_string(contact_list);
	free_attr_list(contact_list);
    }	

    if (quiet <= 0) {
	if (forked) {
	    printf("Forked a communication thread\n");
	} else {
	    printf("Doing non-threaded communication handling\n");
	}
    }
    srand48(1);

    term = EValloc_stone(cm);
    EVassoc_terminal_action(cm, term, output_format_list, output_handler, &message_count);
    filter = create_transform_action_spec(simple_format_list, 
					  output_format_list, trans);
    
    fstone = EValloc_stone(cm);
    faction = EVassoc_immediate_action(cm, fstone, filter, NULL);
    EVaction_set_output(cm, fstone, faction, 0, term);

    args[2] = string_list;
    args[2] = malloc(10 + strlen(string_list) + strlen(filter));
    sprintf(args[2], "%d:%s", fstone, string_list);
    subproc_proc = run_subprocess(args);

    /* give him time to start */
    CMsleep(cm, 10);
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
    CManager_close(cm);
    if (message_count != repeat_count) printf("Message count == %d\n", message_count);
    return !(message_count == repeat_count);
}
