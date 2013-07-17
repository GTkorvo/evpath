#include "config.h"

#include <stdio.h>
#include <atl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "evpath.h"
#include <errno.h>

static atom_t CM_TRANS_TEST_SIZE = 10240;
static atom_t CM_TRANS_TEST_VECS = 4;
static atom_t CM_TRANS_TEST_VERBOSE = -1;
static atom_t CM_TRANS_TEST_REPEAT = 10;
static atom_t CM_TRANS_TEST_REUSE_WRITE_BUFFER = -1;
static atom_t CM_TRANSPORT = -1;
static atom_t CM_TRANS_TEST_RECEIVED_COUNT = -1;

static int vec_count = 4;
static int size = 10240;
static int msg_count = 10;

static int received_count = 0;
static int expected_count = -1;
static int write_size = -1;
static int verbose = 0;
static int size_error = 0;
static int global_exit_condition = -1;
static attr_list global_test_result = NULL;

attr_list
trans_test_upcall(CManager cm, void *buffer, long length, int type, attr_list list)
{
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
	    get_int_attr(list, CM_TRANS_TEST_SIZE, &write_size);
	    get_int_attr(list, CM_TRANS_TEST_VERBOSE, &verbose);
	}
	return NULL;
	break;
    case 1:
	/* body message */
	if (verbose) printf("Body message %d received, length %ld\n", *(int*)buffer, length);
	if (length != (write_size - 12 /* test protocol swallows a little as header */)) {
	    if (verbose) printf("Error in body delivery size, expected %d, got %ld\n", write_size - 12, length);
	    size_error++;
	}
	if (*(int*)buffer != received_count) {
	    static int warned = 0;
	    if (verbose && !warned) {
		printf("Data missing or out of order, expected msg %d and got %d\n", received_count, *(int*)buffer);
		warned++;
	    }
	}
	received_count++;
	return NULL;
	break;
    case 2: {
	/* test finalize */
	attr_list ret = create_attr_list();
	set_int_attr(ret, CM_TRANS_TEST_RECEIVED_COUNT, received_count);
	global_test_result = ret;
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
    printf("USAGE STUFF\n");
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
    argv0 = argv[0];
    int start_subproc_arg_count = 4; /* leave a few open at the beginning */
    char **subproc_args = malloc((argc + start_subproc_arg_count + 2)*sizeof(argv[0]));
    int cur_subproc_arg = start_subproc_arg_count;
    while (argv[1] && (argv[1][0] == '-')) {
	subproc_args[cur_subproc_arg++] = strdup(argv[1]);
	if (argv[1][1] == 'c') {
	    start_subprocess = 0;
	} else if (strcmp(&argv[1][1], "q") == 0) {
	    verbose--;
	} else if (strcmp(&argv[1][1], "v") == 0) {
	    verbose++;
	} else if (strcmp(&argv[1][1], "vectors") == 0) {
	    if (!argv[2] || (sscanf("%d", argv[2], &vec_count) != 1)) {
		printf("Bad -vectors argument \"%s\"\n", argv[2]);
		usage();
	    }
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "size") == 0) {
	    if (!argv[2] || (sscanf("%d", argv[2], &size) != 1)) {
		printf("Bad -size argument \"%s\"\n", argv[2]);
		usage();
	    }
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "msg_count") == 0) {
	    if (!argv[2] || (sscanf("%d", argv[2], &msg_count) != 1)) {
		printf("Bad -msg_count argument \"%s\"\n", argv[2]);
		usage();
	    }
	    argv++; argc--;
	} else if (strcmp(&argv[1][1], "n") == 0) {
	    start_subprocess = 0;
	    verbose = 1;
	}
	argv++;
	argc--;
    }

    cm = CManager_create();
    CMinstall_perf_upcall(cm, trans_test_upcall);
    (void) CMfork_comm_thread(cm);

    if (atom_init == 0) {
	CM_TRANS_TEST_SIZE = attr_atom_from_string("CM_TRANS_TEST_SIZE");
	CM_TRANS_TEST_VECS = attr_atom_from_string("CM_TRANS_TEST_VECS");
	CM_TRANS_TEST_VERBOSE = attr_atom_from_string("CM_TRANS_TEST_VERBOSE");
	CM_TRANS_TEST_REPEAT = attr_atom_from_string("CM_TRANS_TEST_REPEAT");
	CM_TRANS_TEST_REUSE_WRITE_BUFFER = attr_atom_from_string("CM_TRANS_TEST_REUSE_WRITE_BUFFER");
	CM_TRANS_TEST_RECEIVED_COUNT = attr_atom_from_string("CM_TRANS_TEST_RECEIVED_COUNT");
	CM_TRANSPORT = attr_atom_from_string("CM_TRANSPORT");
	atom_init++;
    }


    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	char *transport = NULL;
	if ((transport = getenv("CMTransport")) != NULL) {

	    listen_list = create_attr_list();
	    add_string_attr(listen_list, CM_TRANSPORT, strdup(transport));
	}
	CMlisten_specific(cm, listen_list);
	contact_list = CMget_contact_list(cm);
	if (start_subprocess) {
	    subproc_args[cur_subproc_arg++] = attr_list_to_string(contact_list);
	    subproc_args[cur_subproc_arg] = NULL;
	    subproc_args[--start_subproc_arg_count] = argv0;
	    global_exit_condition = CMCondition_get(cm, NULL);
	    run_subprocess(&subproc_args[start_subproc_arg_count]);
	    CMCondition_wait(cm, global_exit_condition);
	    if (global_test_result) dump_attr_list(global_test_result);
	} else {
	    global_exit_condition = CMCondition_get(cm, NULL);
	    printf("Contact list \"%s\"\n", attr_list_to_string(contact_list));
	    CMCondition_wait(cm, global_exit_condition);
	    printf("Return from condition wait\n");
	}
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
	if (conn == NULL) {
	    printf("No connection\n");
	    exit(1);
	}


	test_list = create_attr_list();	    
	    
	add_int_attr(test_list, CM_TRANS_TEST_SIZE, size); 
	add_int_attr(test_list, CM_TRANS_TEST_VECS, vec_count); 
	add_int_attr(test_list, CM_TRANS_TEST_REPEAT, msg_count); 
		
	result = CMtest_transport(conn, test_list);
    }
    CManager_close(cm);
    return 0;
}
