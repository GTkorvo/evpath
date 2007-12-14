#include "../config.h"

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

#define MSG_COUNT 30 
static int msg_limit = MSG_COUNT;

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
    int vec_count;
    IOEncodeVector vecs;
} simple_rec, *simple_rec_ptr;

IOField event_vec_elem_fields[] =
{
    {"len", "integer", sizeof(((IOEncodeVector)0)[0].iov_len), 
     IOOffset(IOEncodeVector, iov_len)},
    {"elem", "char[len]", sizeof(char), IOOffset(IOEncodeVector,iov_base)},
    {(char *) 0, (char *) 0, 0, 0}
};

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
    {"vec_count", "integer",
     sizeof(int), IOOffset(simple_rec_ptr, vec_count)},
    {"vecs", "EventVecElem[vec_count]", sizeof(struct _io_encode_vec), 
     IOOffset(simple_rec_ptr, vecs)},
    {NULL, NULL, 0, 0}
};

static CMFormatRec simple_format_list[] =
{
    {"simple", simple_field_list},
    {"complex", complex_field_list},
    {"nested", nested_field_list},
    {"EventVecElem", event_vec_elem_fields},
    {NULL, NULL}
};

static int size = 400;
static int vecs = 200;
int quiet = 1;

static void generate_record(simple_rec_ptr event);

typedef struct thread_rec {
    CManager cm;
    int count;
    EVstone target;
    int thread;
} thread_rec;

static
int
submit_thread(void *vrec)
{
    struct thread_rec *rec = vrec;
    simple_rec data;
    attr_list attrs;
    int i;
    EVsource source_handle = EVcreate_submit_handle(rec->cm, rec->target, 
						    simple_format_list);
    for (i=0; i < rec->count; i++) {
	int tmp;
	generate_record(&data);
	tmp = data.short_field;
	attrs = create_attr_list();
#define CMDEMO_TEST_ATOM ATL_CHAR_CONS('C','\115','\104','t')
	set_attr_atom_and_string("CMdemo_test_atom", CMDEMO_TEST_ATOM);
	add_attr(attrs, CMDEMO_TEST_ATOM, Attr_Int4, (attr_value)45678);

	data.short_field = rec->thread;
	data.integer_field += (tmp - data.short_field);
	data.integer_field++;
	data.long_field--;
	EVsubmit(source_handle, &data, attrs);
	CMusleep(rec->cm, lrand48() % 500);
    }
    if (quiet <= 0) printf("Write %d messages\n", msg_limit);
    return 0;
}

static
void 
generate_record(event)
simple_rec_ptr event;
{
    int i;
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
    event->vec_count = vecs;
    event->vecs = malloc(sizeof(event->vecs[0]) * vecs);
    if (quiet <= 0) printf("Sending %d vecs of size %d\n", vecs, size/vecs);
    for (i=0; i < vecs; i++) {
	event->vecs[i].iov_len = size/vecs;
	event->vecs[i].iov_base = malloc(event->vecs[i].iov_len);
    }
}

static int msg_count = 0;

static
int
simple_handler(cm, vevent, client_data, attrs)
CManager cm;
void *vevent;
void *client_data;
attr_list attrs;
{
    simple_rec_ptr event = vevent;
    long sum = 0, scan_sum = 0;
/*    printf("Received event from thread %d\n", event->short_field);*/
    sum += event->integer_field % 100;
    sum += event->short_field % 100;
    sum += event->long_field % 100;
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;
    sum += ((int) (event->double_field * 100.0)) % 100;
    sum += event->char_field;
    sum = sum % 100;
    scan_sum = event->scan_sum;
    if (sum < 0) sum += 100;
    if (sum != scan_sum) {
	printf("Received record checksum does not match. expected %d, got %d\n",
	       (int) sum, (int) scan_sum);
    }
    msg_count++;
    usleep(10000);
    if ((quiet <= -1) || (sum != scan_sum)) {
	printf("In the handler, event data is :\n");
	printf("	integer_field = %d\n", event->integer_field);
	printf("	short_field = %d\n", event->short_field);
	printf("	long_field = %ld\n", event->long_field);
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

int
main(argc, argv)
int argc;
char **argv;
{
    CManager cm;
    int regression_master = 1;
    int forked = 0;

    while (argv[1] && (argv[1][0] == '-')) {
	if (strcmp(&argv[1][1], "size") == 0) {
	    if (sscanf(argv[2], "%d", &size) != 1) {
		printf("Unparseable argument to -size, %s\n", argv[2]);
	    }
	    if (vecs == 0) { vecs = 1; printf("vecs not 1\n");}
	    argv++;
	    argc--;
	} else 	if (strcmp(&argv[1][1], "vecs") == 0) {
	    if (sscanf(argv[2], "%d", &vecs) != 1) {
		printf("Unparseable argument to -vecs, %s\n", argv[2]);
	    }
	    argv++;
	    argc--;
	} else if (argv[1][1] == 'c') {
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
/*    forked = CMfork_comm_thread(cm);*/
    if (quiet <= 0) {
	if (forked) {
	    printf("Forked a communication thread\n");
	} else {
	    printf("Doing non-threaded communication handling\n");
	}
    }

    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	char *transport = NULL;
	char *postfix = NULL;
	char *string_list;
	EVstone stone;
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
	    set_int_attr(contact_list, CM_CONN_BLOCKING, 0);
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
/*	    conn = CMinitiate_conn(cm, contact_list);*/
	    string_list = attr_list_to_string(contact_list);
	    free_attr_list(contact_list);
	}	
	stone = EValloc_stone(cm);
	EVassoc_terminal_action(cm, stone, simple_format_list, simple_handler, NULL);
	printf("Contact list \"%d:%s\"\n", stone, string_list);
	while(msg_count != msg_limit) {
	    sleep(2);
	    CMpoll_network(cm);
	    printf("Received %d messages\n", msg_count);
	}
    } else {
	int remote_stone, stone = 0;
	int i;
	struct thread_rec thr_rec[3];
	thr_thread_t thr0, thr1, thr2;
	void *status;

	if (argc == 2) {
	    attr_list contact_list;
	    char *list_str;
	    sscanf(argv[1], "%d:", &remote_stone);
	    list_str = strchr(argv[1], ':') + 1;
	    contact_list = attr_list_from_string(list_str);
	    stone = EValloc_stone(cm);
	    EVassoc_output_action(cm, stone, contact_list, remote_stone);
	}
	
	for (i=0; i < 3; i++) {
	    thr_rec[i].target = stone;
	    thr_rec[i].cm = cm;
	    thr_rec[i].thread = i;
	    thr_rec[i].count = msg_limit / 3;
	}
	thr_rec[2].count = msg_limit - thr_rec[0].count - thr_rec[1].count;

	CMfork_comm_thread(cm);
	thr0 = thr_fork(submit_thread, &thr_rec[0]);
	thr1 = thr_fork(submit_thread, &thr_rec[1]);
	thr2 = thr_fork(submit_thread, &thr_rec[2]);
	thr_thread_join(thr0, &status);
	thr_thread_join(thr1, &status);
	thr_thread_join(thr2, &status);

	CMsleep(cm, 10);
    }
    CManager_close(cm);
    return 0;
}

static pid_t subproc_proc = 0;

static void
fail_and_die(signal)
int signal;
{
    fprintf(stderr, "block_test failed to complete in reasonable time\n");
    if (subproc_proc != 0) {
	kill(subproc_proc, 9);
    }
    exit(1);
}

static
pid_t
run_subprocess(args)
char **args;
{
#ifdef HAVE_WINDOWS_H
    int child;
    child = _spawnv(_P_NOWAIT, "./block_test.exe", args);
    if (child == -1) {
	printf("failed for block_test\n");
	perror("spawnv");
    }
    return child;
#else
#if 1
    pid_t child = fork();
    if (child == 0) {
	/* I'm the child */
	execv("./block_test", args);
    }
    return child;
#else
    int count = 0;
    printf("Would have run \"");
    while (args[count] != NULL) printf("%s ", args[count++]);
    printf("\"\n");
#endif
#endif
}

static int
do_regression_master_test()
{
    CManager cm;
    char *args[] = {"block_test", "-c", NULL, NULL, NULL, NULL, NULL, NULL, NULL};
    int exit_state;
    int forked = 0;
    int i;
    attr_list contact_list, listen_list = NULL;
    char *string_list, *transport, *postfix;
    char size_str[4];
    char vec_str[4];
    EVstone handle;
    int message_count = 0;
    int expected_count = msg_limit;
    int done = 0;
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
/*    forked = CMfork_comm_thread(cm);*/
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
	set_int_attr(contact_list, CM_CONN_BLOCKING, 0);
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
    args[2] = "-size";
    sprintf(&size_str[0], "%d", size);
    args[3] = size_str;
    args[4] = "-vecs";
    sprintf(&vec_str[0], "%d", vecs);
    args[5] = vec_str;
    i = 6;
    if (quiet <= 0) {
	args[i++] = "-v";
	if (forked) {
	    printf("Forked a communication thread\n");
	} else {
	    printf("Doing non-threaded communication handling\n");
	}
    }
    srand48(1);

    handle = EValloc_stone(cm);
    EVassoc_terminal_action(cm, handle, simple_format_list, simple_handler, &message_count);
    args[i] = malloc(strlen(string_list) + 10);

    sprintf(args[i], "%d:%s", handle, string_list);
    subproc_proc = run_subprocess(args);

    if (quiet <= 0) {
	printf("Waiting for remote....\n");
    }
    while (!done) {
#ifdef HAVE_WINDOWS_H
	if (_cwait(&exit_state, subproc_proc, 0) == -1) {
	    perror("cwait");
	}
	if (exit_state == 0) {
	    if (quiet <= 0) 
		printf("Subproc exitted\n");
	} else {
	    printf("Single remote subproc exit with status %d\n",
		   exit_state);
	}
#else
	int result, i;
	if (quiet <= 0) {
	    printf(",");
	    fflush(stdout);
	}
	for (i = 0 ; i < 50 ; i++) {
	    sleep(1);
	    CMpoll_network(cm);
	} done++;
/*	CMsleep(cm, 50);	done++;*/

	result = waitpid(subproc_proc, &exit_state, WNOHANG);
	if (result == -1) {
	    perror("waitpid");
	    done++;
	}
	if (result == subproc_proc) {
	    if (WIFEXITED(exit_state)) {
		if (WEXITSTATUS(exit_state) == 0) {
		    if (quiet <= 0) 
			printf("Subproc exited\n");
		} else {
		    printf("Single remote subproc exit with status %d\n",
			   WEXITSTATUS(exit_state));
		}
	    } else if (WIFSIGNALED(exit_state)) {
		printf("Single remote subproc died with signal %d\n",
		       WTERMSIG(exit_state));
	    }
	    done++;
	}
    }
#endif
    if (msg_count != msg_limit) {
	int i = 10;
	while ((i >= 0) && (msg_count != msg_limit)) {
	    sleep(1);
	    CMpoll_network(cm);
/*	    CMsleep(cm, 1);*/
	}
    }
    free(args[6]);
    free(string_list);
    CManager_close(cm);
    if (message_count != expected_count) {
	printf ("failure, received %d messages instead of %d\n",
		message_count, expected_count);
    }
    return !(message_count == expected_count);
}
