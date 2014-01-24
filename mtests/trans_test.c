#include "config.h"

#include <stdio.h>
#include <atl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "evpath.h"
#include <errno.h>

static atom_t CM_TRANS_TEST_SIZE = 10240;
static atom_t CM_TRANS_TEST_VECS = 4;
static atom_t CM_TRANS_TEST_VERBOSE = -1;
static atom_t CM_TRANS_TEST_REPEAT = 10;
static atom_t CM_TRANS_TEST_REUSE_WRITE_BUFFER = -1;
static atom_t CM_TRANS_TEST_DURATION = -1;
static atom_t CM_TRANS_MEGABITS_SEC = -1;
static atom_t CM_TRANS_TEST_TAKE_RECEIVE_BUFFER = -1;
static atom_t CM_TRANSPORT = -1;
static atom_t CM_TRANS_TEST_RECEIVED_COUNT = -1;
static atom_t CM_TRANS_TEST_TAKEN_CORRUPT = -1;

static int vec_count = 4;
static long size = 10240;
static int msg_count = 10;
static int reuse_write = 1;

static int received_count = 0;
static int taken_corrupt = 0;
static int expected_count = -1;
static long write_size = -1;
static int verbose = 0;
static int take = 0;
static int size_error = 0;
static int global_exit_condition = -1;
static attr_list global_test_result = NULL;

typedef struct _buf_list {
    int checksum;
    long length;
    void *buffer;
} *buf_list;

attr_list
trans_test_upcall(CManager cm, void *buffer, long length, int type, attr_list list)
{
    static buf_list buffer_list = NULL;
    static int buffer_count = 0;
    switch(type) {
    case 0:
	/* test init */
	if (verbose) {
	    printf("Transport test init - attributes :  ");
	    dump_attr_list(list);
	    printf("\n");
	}
	received_count = 0;
	if (list) {
	    get_int_attr(list, CM_TRANS_TEST_REPEAT, &expected_count);
	    get_long_attr(list, CM_TRANS_TEST_SIZE, &write_size);
	    get_int_attr(list, CM_TRANS_TEST_VERBOSE, &verbose);
	    get_int_attr(list, CM_TRANS_TEST_TAKE_RECEIVE_BUFFER, &take);
	}
	return NULL;
	break;
    case 1:
	/* body message */
	if (verbose) printf("Body message %d received, length %ld\n", *(int*)buffer, length);
	if (length != (write_size - 12 /* test protocol swallows a little as header */)) {
	    printf("Error in body delivery size, expected %ld, got %ld\n", write_size - 12, length);
	    size_error++;
	}
	if (*(int*)buffer != received_count) {
	    static int warned = 0;
	    if (verbose && !warned) {
		printf("Data missing or out of order, expected msg %d and got %d\n", received_count, *(int*)buffer);
		warned++;
	    }
	}
	if (take) {
	    int sum = 0;
	    int i;
	    if (!buffer_list) {
		buffer_list = malloc(sizeof(buffer_list[0]));
	    } else {
		buffer_list = realloc(buffer_list, (buffer_count+1)*sizeof(buffer_list[0]));
	    }
	    for(i=0; i < length/sizeof(int); i++) {
		sum += ((int*)buffer)[i];
	    }
	    buffer_list[buffer_count].buffer = buffer;
	    buffer_list[buffer_count].length = length;
	    buffer_list[buffer_count].checksum = sum;
	    CMtake_buffer(cm, buffer);
	    buffer_count++;
	}
	received_count++;
	return NULL;
	break;
    case 2: {
	/* test finalize */
	attr_list ret = create_attr_list();
	double secs;
	int buf;
	for (buf = 0; buf < buffer_count; buf++) {
	    int sum = 0;
	    int i;
	    for(i=0; i < buffer_list[buf].length/sizeof(int); i++) {
		sum += ((int*)buffer_list[buf].buffer)[i];
	    }
	    if (buffer_list[buf].checksum != sum) {
		printf("taken data corrupted, calc checksum %d, stored %d, first entry %d\n", sum, buffer_list[buf].checksum, ((int*)buffer_list[buf].buffer)[0]);
		taken_corrupt++;
	    }
	    CMreturn_buffer(cm, buffer_list[buf].buffer);
	}
	if (buffer_list) {
	    free(buffer_list);
	    buffer_list = NULL;
	}
	set_int_attr(ret, CM_TRANS_TEST_RECEIVED_COUNT, received_count);
	//dump_attr_list(list);
	if (get_double_attr(list, CM_TRANS_TEST_DURATION, &secs)) {
	    long size = received_count * write_size;
	    double megabits = (double)size*8 / ((double)1000*1000);
	    double megabits_sec = megabits / secs;
	    set_double_attr(ret, CM_TRANS_MEGABITS_SEC, megabits_sec);
	    set_double_attr(ret, CM_TRANS_TEST_DURATION, secs);
//	    printf("Megabits/sec is %g\n", megabits_sec);
	} else {
	    printf("No test duration attr\n");
	}
	if (taken_corrupt) {
	    set_int_attr(ret, CM_TRANS_TEST_TAKEN_CORRUPT, taken_corrupt);
	}
	global_test_result = ret;
	add_ref_attr_list(ret);
	if (global_exit_condition != -1) {
	    CMCondition_signal(cm, global_exit_condition);
	}
	return ret;
	break;
    }
    default:
	printf("Bad type in trans_test_upcall, %d\n", type);
	return NULL;
    }
}

static char *argv0;
static pid_t subproc_proc = 0;
static int timeout = 60;

static void
fail_and_die(int signal)
{
    (void)signal;
    fprintf(stderr, "Trans_test failed to complete in reasonable time, increase from -timeout %d\n", timeout);
    fprintf(stderr, "Stats are : vec_count = %d, size = %ld, msg_count = %d, reuse_write = %d, received_count = %d\n", vec_count, size, msg_count, reuse_write, received_count);
    fprintf(stderr, "    reuse_write = %d, received_count = %d, taken_corrupt = %d, expected_count = %d, write_size = %ld, size_error = %d\n", reuse_write, received_count, taken_corrupt, expected_count, write_size, size_error);
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
    child = _spawnv(_P_NOWAIT, args[0], args);
    if (child == -1) {
	printf("failed for cmtest\n");
	perror("spawnv");
    }
    return child;
#else
    pid_t child = fork();
/*    int i = 0;
    printf("Running : ");
    while (args[i] != NULL) {
	printf("%s ", args[i++]);
	
    }
    printf("\n");*/
    if (child == 0) {
	/* I'm the child */
	execv(args[0], args);
    }
    return child;
#endif
}

static void
usage()
{
    printf("Usage:  trans_test <options> \n");
    printf("  Options:\n");
    printf("\t-q  quiet\n");
    printf("\t-v  verbose\n");
    printf("\t-vectors <count>  Number of separate data blocks within test data.\n");
    printf("\t-size <byte count>  Overall size of test messages.\n");
    printf("\t\tbyte count can be <int> for bytes, <int>k for kilobytes (1000),\n");
    printf("\t\t<int>m for megabytes (1,000,000),\n\t\t<int>g for gigabytes (1,000,000,000)\n");
    printf("\t-ssh <hostname>  host to use for remote client via ssh.\n");
    printf("\t-transport <trans>  which CM/EVPath transport to use.\n");
    printf("\t-msg_count <count>  how many messages to send.\n");
    printf("\t-timeout <seconds>  how long to wait before declaring failure.\n");
    printf("\t-reuse_write_buffers <0/1>  should the same buffer be sent \n\t\tmultiple times or different buffers?\n");
    printf("\t-take_receive_buffer <0/1>  should the receiving buffer be \n\t\ttaken out of service upon receipt?\n");
    printf("\t-n  No regression test.  I.E. just run the master and print \n\t\twhat command would have been run for client.\n");

    exit(1);
}

int
main(argc, argv)
    int argc;
    char **argv;
{
    CManager cm;
    CMConnection conn = NULL;
    CMFormat format;
    static int atom_init = 0;
    int start_subprocess = 1;
    int ret, actual_count;
    argv0 = argv[0];
    int start_subproc_arg_count = 4; /* leave a few open at the beginning */
    char **subproc_args = calloc((argc + start_subproc_arg_count + 2), sizeof(argv[0]));
    int cur_subproc_arg = start_subproc_arg_count;
    char *transport = NULL;
    char path[10240];

    (void)getcwd(&path[0], sizeof(path));
    if (argv0[0] != '/') {
	strcat(path, "/");
	strcat(path, argv0);
    } else {
	strcpy(path, argv0);
    }
    subproc_args[--start_subproc_arg_count] = strdup(path);
    while (argv[1] && (argv[1][0] == '-')) {
	subproc_args[cur_subproc_arg++] = strdup(argv[1]);
	if (argv[1][1] == 'c') {
	    start_subprocess = 0;
	} else if (strcmp(&argv[1][1], "q") == 0) {
	    verbose--;
	} else if (strcmp(&argv[1][1], "v") == 0) {
	    verbose++;
	} else if (strcmp(&argv[1][1], "h") == 0) {
	    usage();
	    exit(0);
	} else if (strcmp(&argv[1][1], "vectors") == 0) {
	    if (!argv[2] || (sscanf(argv[2], "%d", &vec_count) != 1)) {
		printf("Bad -vectors argument \"%s\"\n", argv[2]);
		usage();
	    }
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "size") == 0) {
	    char *endptr;
	    if (!argv[2]) {
		printf("Bad -size argument \"%s\"\n", argv[2]);
		usage();
	    }
	    size = strtol(argv[2], &endptr, 10);
	    if (endptr == argv[2]) {
		printf("Bad -size argument \"%s\"\n", argv[2]);
		usage();
	    }
	    if ((strcmp(endptr, "k") == 0) || (strcmp(endptr, "K") == 0)) {
		size *= 1000;
	    } else if ((strcmp(endptr, "m") == 0) || (strcmp(endptr, "M") == 0)) {
		size *= 1000 * 1000;
	    } else if ((strcmp(endptr, "g") == 0) || (strcmp(endptr, "G") == 0)) {
		size *= 1000 * 1000 * 1000;
	    } else {
		if (endptr[0] != 0) {
		    printf("Unknownn postfix to size digits, \"%s\"\n", endptr);
		}
	    }
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "ssh") == 0) {
	    char *destination_host;
	    if (!argv[2]) {
		printf("Missing --ssh destination\n");
		usage();
	    }
	    destination_host = strdup(argv[2]);
	    start_subproc_arg_count-=2;
	    subproc_args[start_subproc_arg_count] = strdup(SSH_PATH);
	    subproc_args[start_subproc_arg_count+1] = destination_host;
	    cur_subproc_arg--;
	    free(subproc_args[cur_subproc_arg]);
	    subproc_args[cur_subproc_arg] = NULL;
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "transport") == 0) {
	    if (!argv[2]) {
		printf("missing -transport\n");
		usage();
	    }
	    transport = strdup(argv[2]);
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "msg_count") == 0) {
	    if (!argv[2] || (sscanf(argv[2], "%d", &msg_count) != 1)) {
		printf("Bad -msg_count argument \"%s\"\n", argv[2]);
		usage();
	    }
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "timeout") == 0) {
	    if (!argv[2] || (sscanf(argv[2], "%d", &timeout) != 1)) {
		printf("Bad -timeout argument \"%s\"\n", argv[2]);
		usage();
	    }
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "reuse_write_buffers") == 0) {
	    if (!argv[2] || (sscanf(argv[2], "%d", &reuse_write) != 1)) {
		printf("Bad -reuse_write argument \"%s\"\n", argv[2]);
		usage();
	    }
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "take_receive_buffer") == 0) {
	    if (!argv[2] || (sscanf(argv[2], "%d", &take) != 1)) {
		printf("Bad -take_receive_buffer argument \"%s\"\n", argv[2]);
		usage();
	    }
	    subproc_args[cur_subproc_arg++] = strdup(argv[2]);
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "n") == 0) {
	    start_subprocess = 0;
	    verbose = 1;
	    timeout = 600;
	} else {
	    printf("Argument not recognized, \"%s\"\n", argv[1]);
	    usage();
	}
	argv++;
	argc--;
    }

    struct sigaction sigact;
    sigact.sa_flags = 0;
    sigact.sa_handler = fail_and_die;
    sigemptyset(&sigact.sa_mask);
    sigaddset(&sigact.sa_mask, SIGALRM);
    sigaction(SIGALRM, &sigact, NULL);
    alarm(timeout);

    cm = CManager_create();
    CMinstall_perf_upcall(cm, trans_test_upcall);
    (void) CMfork_comm_thread(cm);

    if (atom_init == 0) {
	CM_TRANS_TEST_SIZE = attr_atom_from_string("CM_TRANS_TEST_SIZE");
	CM_TRANS_TEST_VECS = attr_atom_from_string("CM_TRANS_TEST_VECS");
	CM_TRANS_TEST_VERBOSE = attr_atom_from_string("CM_TRANS_TEST_VERBOSE");
	CM_TRANS_TEST_REPEAT = attr_atom_from_string("CM_TRANS_TEST_REPEAT");
	CM_TRANS_TEST_REUSE_WRITE_BUFFER = attr_atom_from_string("CM_TRANS_TEST_REUSE_WRITE_BUFFER");
	CM_TRANS_TEST_TAKE_RECEIVE_BUFFER = attr_atom_from_string("CM_TRANS_TEST_TAKE_RECEIVE_BUFFER");
	CM_TRANS_TEST_RECEIVED_COUNT = attr_atom_from_string("CM_TRANS_TEST_RECEIVED_COUNT");
	CM_TRANS_TEST_TAKEN_CORRUPT = attr_atom_from_string("CM_TRANS_TEST_TAKEN_CORRUPT");
	CM_TRANS_TEST_DURATION = attr_atom_from_string("CM_TRANS_TEST_DURATION_SECS");
	CM_TRANS_MEGABITS_SEC = attr_atom_from_string("CM_TRANS_MEGABITS_SEC");
	CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
	atom_init++;
    }


    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	if (transport == NULL) {
	    transport = getenv("CMTransport");
	}
	if (transport != NULL) {
	    listen_list = create_attr_list();
	    add_string_attr(listen_list, CM_TRANSPORT, strdup(transport));
	}
	CMlisten_specific(cm, listen_list);
	contact_list = CMget_contact_list(cm);
	if (contact_list == NULL) {
	    printf("Attribute lists resulted in no listen info!\n");
	    dump_attr_list(listen_list);
	}
	free_attr_list(listen_list);
	subproc_args[cur_subproc_arg++] = attr_list_to_string(contact_list);
	subproc_args[cur_subproc_arg] = NULL;
	global_exit_condition = CMCondition_get(cm, NULL);
	if (start_subprocess) {
	    subproc_proc = run_subprocess(&subproc_args[start_subproc_arg_count]);
	    CMCondition_wait(cm, global_exit_condition);
	    if (global_test_result) {
		double secs, mbps;
		get_double_attr(global_test_result, CM_TRANS_TEST_DURATION, &secs);
		get_double_attr(global_test_result, CM_TRANS_MEGABITS_SEC, &mbps);
		printf("transport = %s size = %ld, count = %d, secs = %g, Mbps = %g\n",
		       transport, size, msg_count, secs, mbps);
	    }
	} else {
	    int i;
	    printf("Would have run: \n");
	    for (i=start_subproc_arg_count; i<cur_subproc_arg; i++) {
		printf(" %s", subproc_args[i]);
	    }
	    printf("\n");
	    CMCondition_wait(cm, global_exit_condition);
	}
	free_attr_list(contact_list);
    } else {
	int i;

	attr_list contact_list = NULL;
	attr_list test_list, result;

	for (i = 1; i < argc; i++) {
	    contact_list = attr_list_from_string(argv[i]);
	    if (contact_list == NULL) {
		printf("Remaining Argument \"%s\" not recognized as size or contact list\n",
		       argv[i]);
		usage();
	    }
	}
	if (contact_list == NULL) {
	    exit(1);
	}
	conn = CMinitiate_conn(cm, contact_list);
	free_attr_list(contact_list);
	if (conn == NULL) {
	    printf("No connection using contact list %s\n", argv[1]);
	    exit(1);
	}


	test_list = create_attr_list();	    
	    
	add_int_attr(test_list, CM_TRANS_TEST_SIZE, size); 
	add_int_attr(test_list, CM_TRANS_TEST_VECS, vec_count); 
	add_int_attr(test_list, CM_TRANS_TEST_REPEAT, msg_count); 
	add_int_attr(test_list, CM_TRANS_TEST_REUSE_WRITE_BUFFER, reuse_write); 
	add_int_attr(test_list, CM_TRANS_TEST_TAKE_RECEIVE_BUFFER, take); 
	add_int_attr(test_list, CM_TRANS_TEST_VERBOSE, verbose);
		
	result = CMtest_transport(conn, test_list);
	global_test_result = result;
	free_attr_list(test_list);
    }
    CMsleep(cm, 2);
    CManager_close(cm);
    if (transport) free(transport);
    {
	int i;
	for (i=0; i < cur_subproc_arg; i++) {
	    if (subproc_args[i]) {
		free(subproc_args[i]);
	    }
	}
    }
    free(subproc_args);

    if (!global_test_result) return 1;
    ret = 0;
    if (!get_int_attr(global_test_result, CM_TRANS_TEST_RECEIVED_COUNT, &actual_count)) ret = 1;
    if (actual_count != msg_count) ret = 1;
    free_attr_list(global_test_result);
    return ret;
}

