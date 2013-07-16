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

int
trans_test_upcall(CManager cm, void *buffer, int type, attr_list list)
{
    printf("Upcall happend %d- ", type);
    if (list) dump_attr_list(list);
    printf(" - \n");
    return 0;
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

    cm = CManager_create();
    CMinstall_perf_upcall(cm, trans_test_upcall);
    (void) CMfork_comm_thread(cm);

    static atom_t CM_TRANS_TEST_SIZE = -1;
    static atom_t CM_TRANS_TEST_VECS = -1;
    static atom_t CM_TRANS_TEST_VERBOSE = -1;
    static atom_t CM_TRANS_TEST_REPEAT = -1;
    static atom_t CM_TRANS_TEST_REUSE_WRITE_BUFFER = -1;
    static atom_t CM_TRANSPORT = -1;

    if (atom_init == 0) {
	CM_TRANS_TEST_SIZE = attr_atom_from_string("CM_TRANS_TEST_SIZE");
	CM_TRANS_TEST_VECS = attr_atom_from_string("CM_TRANS_TEST_VECS");
	CM_TRANS_TEST_VERBOSE = attr_atom_from_string("CM_TRANS_TEST_VERBOSE");
	CM_TRANS_TEST_REPEAT = attr_atom_from_string("CM_TRANS_TEST_REPEAT");
	CM_TRANS_TEST_REUSE_WRITE_BUFFER = attr_atom_from_string("CM_TRANS_TEST_REUSE_WRITE_BUFFER");
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
	printf("Contact list \"%s\"\n", attr_list_to_string(contact_list));
	CMsleep(cm, 1200);
    } else {
	int size = 100;
	int i,j;
	int N, repeat_time, size_inc;
	int bw_long, bw_cof; /*measured values*/

	attr_list contact_list = NULL;
	attr_list list, result_list;

	for (i = 1; i < argc; i++) {
	    char *final;
	    long value;
	    errno = 0;
	    value = strtol(argv[i], &final, 10);
	    if ((errno == 0) && (final == (argv[i] + strlen(argv[i])))) {
		/* valid number as an argument, must be byte size */
		size = (int) value;
	    } else {
		contact_list = attr_list_from_string(argv[i]);
		if (contact_list == NULL) {
		    printf("Argument \"%s\" not recognized as size or contact list\n",
			   argv[i]);
		}
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


	list = create_attr_list();	    
	    
	add_int_attr(list, CM_TRANS_TEST_SIZE, 10240); 
	add_int_attr(list, CM_TRANS_TEST_VECS, 4); 
	add_int_attr(list, CM_TRANS_TEST_REPEAT, 4); 
		
	CMtest_transport(conn, list);
    }
    CManager_close(cm);
    return 0;
}
