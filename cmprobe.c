#include "config.h"

#include <stdio.h>
#include <atl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>
#include "cm.h"
#include "gen_thread.h"
#include "libltdl/ltdl.h"
#include <errno.h>
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#define drand48() (((double)rand())/((double)RAND_MAX))
#define lrand48() rand()
#define srand48(x)
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

static CMFormatRec simple_format_list[] =
{
    {"complex", complex_field_list},
    {"nested", nested_field_list},
    {NULL, NULL}
};

int quiet = 1;

static
void
simple_handler(cm, conn, vevent, client_data, attrs)
    CManager cm;
    CMConnection conn;
    void *vevent;
    void *client_data;
    attr_list attrs;
{
    simple_rec_ptr event = vevent;
    long sum = 0, scan_sum = 0;
    sum += event->integer_field % 100;
    sum += event->short_field % 100;
    sum += event->long_field % 100;
    sum += ((int) (event->nested_field.item.r * 100.0)) % 100;
    sum += ((int) (event->nested_field.item.i * 100.0)) % 100;
    sum += ((int) (event->double_field * 100.0)) % 100;
    sum += event->char_field;
    sum = sum % 100;
    scan_sum = event->scan_sum;
    if (sum != scan_sum) {
	printf("Received record checksum does not match. expected %d, got %d\n",
	       (int) sum, (int) scan_sum);
    }
    if ((quiet <= 0) || (sum != scan_sum)) {
	printf("In the handler, event data is :\n");
	printf("	integer_field = %d\n", event->integer_field);
	printf("	short_field = %d\n", event->short_field);
	printf("	long_field = %ld\n", event->long_field);
	printf("	double_field = %g\n", event->double_field);
	printf("	char_field = %c\n", event->char_field);
    }
    if (client_data != NULL) {
	int tmp = *((int *) client_data);
	if (tmp > 0) {
	    *((int *) client_data) = tmp - 1;
	}
    }
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

    LTDL_SET_PRELOADED_SYMBOLS();
    srand48(getpid());
#ifdef USE_PTHREADS
    gen_pthread_init();
#endif
    cm = CManager_create();
    (void) CMfork_comm_thread(cm);


    if (atom_init == 0) {
	set_attr_atom_and_string("CM_REBWM_RLEN", CM_REBWM_RLEN);
	set_attr_atom_and_string("CM_REBWM_REPT", CM_REBWM_REPT);
	set_attr_atom_and_string("CM_BW_MEASURE_INTERVAL", CM_BW_MEASURE_INTERVAL);	
	set_attr_atom_and_string("CM_BW_MEASURE_SIZE", CM_BW_MEASURE_SIZE);	
	set_attr_atom_and_string("CM_BW_MEASURE_SIZEINC", CM_BW_MEASURE_SIZEINC);
	set_attr_atom_and_string("CM_BW_MEASURED_VALUE", CM_BW_MEASURED_VALUE);	
	set_attr_atom_and_string("CM_BW_MEASURED_COF", CM_BW_MEASURED_COF);

	atom_init++;
    }


    if (argc == 1) {
	attr_list contact_list, listen_list = NULL;
	char *transport = NULL;
	if ((transport = getenv("CMTransport")) != NULL) {

	    listen_list = create_attr_list();
	    add_attr(listen_list, CM_TRANSPORT, Attr_String,
		     (attr_value) strdup(transport));
	}
	CMlisten_specific(cm, listen_list);
	contact_list = CMget_contact_list(cm);
	printf("Contact list \"%s\"\n", attr_list_to_string(contact_list));
	format = CMregister_format(cm, "simple", simple_field_list,
				   simple_format_list);
	CMregister_handler(format, simple_handler, NULL);
	CMsleep(cm, 1200);
    } else {
	int size;
	int i,j;
	int N, repeat_time, size_inc;
	int bw_long, bw_cof; /*measured values*/

	attr_list contact_list = NULL;
	attr_list bw_list, result_list;

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


	N=3;
	repeat_time=2;
	size=5000; 
	size_inc=2000;
    
	bw_list = create_attr_list();	    
	    
	/*Each measurement done by CMregressive_probe_bandwidth uses N streams of different size, each stream is sent out for repeat_time times. */
	/*For scheduled measurment, Each measurement is done every 2 seconds*/
	add_attr(bw_list, CM_REBWM_RLEN,  Attr_Int4,  (attr_value)(long)N);
	add_attr(bw_list, CM_REBWM_REPT,  Attr_Int4,  (attr_value)(long)repeat_time);
	add_attr(bw_list, CM_BW_MEASURE_INTERVAL, Attr_Int4, (attr_value)2);
	add_attr(bw_list, CM_BW_MEASURE_SIZE, Attr_Int4, (attr_value)(long)size); 
	add_attr(bw_list, CM_BW_MEASURE_SIZEINC, Attr_Int4, (attr_value)(long)size_inc);
		
	CMConnection_set_character(conn, bw_list);
	for(j=0; j<2000; j++)
	{
	    sleep(1);
	    result_list=CMConnection_get_attrs(conn);
	    if (query_attr(result_list, CM_BW_MEASURED_VALUE, NULL,
			   (attr_value *) (long) &bw_long)) {
		printf("BW get from attr: %d\n", bw_long);
	    }else{
		printf("Failed to get bw from attr\n");
	    }

	    if (query_attr(result_list, CM_BW_MEASURED_COF, NULL,
			   (attr_value *) (long) &bw_cof)) {
		printf("BW cof get from attr: %d\n", bw_cof);
	    }else{
		printf("Failed to get bw cof from attr\n");
	    }
	}

/* Example when invoking CMregressive_probe_bandwidth on demand is needed:
   for(i=1; i<120; i++)
   {
   double bandwidth;

   bandwidth=CMregressive_probe_bandwidth(conn, size, contact_list);
   printf("Estimated bandwidth is %f Mbps\n", bandwidth);
   if(bandwidth<0)
   size+=size_inc;
   }
*/

    }
    CManager_close(cm);
    return 0;
}
