#include "evpath.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct _simple_rec {
    int integer_field;
    int array_size;
    double *array;
    char str[4096]; 
} simple_rec, *simple_rec_ptr;

typedef struct _meta_data_server{
    int num_array_dim;
    int *dim_array_size;
    int *dim_proc_size;
    int addr_size;
    char *addr;

} meta_data_server, *meta_data_server_ptr;

meta_data_server array_info;

static FMField simple_field_list[] =
{
    {"integer_field", "integer", sizeof(int), FMOffset(simple_rec_ptr, integer_field)},
    {"array_size", "integer", sizeof(int), FMOffset(simple_rec_ptr, array_size)},
    {"array", "double[array_size]", sizeof(double), FMOffset(simple_rec_ptr, array)},
    {"str", "char[4096]", sizeof(char), FMOffset(simple_rec_ptr, str)},

    {NULL, NULL, 0, 0}
};


static FMStructDescRec simple_format_list[] =
{
    {"simple", simple_field_list, sizeof(simple_rec), NULL},
    {NULL, NULL}
};

static FMField metadata_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(meta_data_server_ptr, num_array_dim)},
    {"dim_array_size", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_server_ptr, dim_array_size)},
    {"dim_proc_size", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_server_ptr, dim_proc_size)},
     {"addr_size", "integer", sizeof(int), FMOffset(meta_data_server_ptr, addr_size)},
   {"str", "char[addr_size]", sizeof(char), FMOffset(meta_data_server_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec metadata_format_list[] =
{
    {"meta_server", metadata_field_list, sizeof(meta_data_server), NULL},
    {NULL, NULL}
};


typedef struct _array_data{
    int num_array_dim;
    int *local_dim;
    int *global_dim;
    int *start;
    int *end;
    int total_local_size;
    char *addr;
} array_data, *array_data_ptr;

static FMField arraydata_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(array_data_ptr, num_array_dim)},
    {"local_dim", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, local_dim)},
    {"global_dim", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, global_dim)},
    {"start", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, start)},
   {"end", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, end)},
    {"total_local_size", "integer", sizeof(int), FMOffset(array_data_ptr, total_local_size)},
   {"addr", "char[total_local_size]", sizeof(char), FMOffset(array_data_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec arraydata_format_list[] =
{
    {"array", arraydata_field_list, sizeof(array_data), NULL},
    {NULL, NULL}
};




enum status {success,failure} status_t;

enum status ev_send(CManager cm,char* contact_info, void* data){
 
   int i;
  EVsource source;
	EVstone remote_stone, output_stone;
	attr_list contact_list;
  EVstone split_stone;
  EVaction split_action;

	char *filter_spec;

	char string_list[2048];
 
    split_stone = EValloc_stone(cm);
    split_action = EVassoc_split_action(cm, split_stone, NULL);

    if (sscanf(contact_info, "%d:%s", &remote_stone, &string_list[0]) != 2) {
	    printf("Bad argument \"%s\"\n", contact_info);
	    exit(0);
	}


	filter_spec = strchr(string_list, ':');
//  printf("string list %s\n, filter_spec = %s\n", string_list, filter_spec);
	if (filter_spec != NULL) {	/* if there is a filter spec */
	    *filter_spec = 0;           /* terminate the contact list */
	    filter_spec++;		/* advance pointer to string start */
      printf("\n\n\n BEFORE decode \n\n");
      printf("string list %s\n, filter_spec = %s\n", string_list, filter_spec);

	    atl_base64_decode((unsigned char *)filter_spec, NULL);  /* decode in place */
	    printf("String list is %s\n", string_list);
	}



	/* regardless of filtering or not, we'll need an output stone */
	output_stone = EValloc_stone(cm);
	contact_list = attr_list_from_string(string_list);
	//printf("This is the contact list   --------");
//	dump_attr_list(contact_list);
	EVassoc_bridge_action(cm, output_stone, contact_list, remote_stone);

	if (filter_spec == NULL) {
	    EVaction_add_split_target(cm, split_stone, split_action, output_stone);
	} else {
	    EVstone filter_stone = EValloc_stone(cm);
	    EVaction filter_action = EVassoc_immediate_action(cm, filter_stone, filter_spec, NULL);
	    EVaction_set_output(cm, filter_stone, filter_action, 0, output_stone);
	    EVaction_add_split_target(cm, split_stone, split_action, filter_stone);
	}
    source = EVcreate_submit_handle(cm, split_stone, metadata_format_list);

    meta_data_server_ptr event = data;
    printf("going to send contact %s\n", event->addr);    
   	EVsubmit(source, data, NULL);
}


static int
request_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    
//    event->array = (double*) malloc(event->array_size*sizeof(double));

    printf("I got %d, array_dim %d, string %s\n", event->integer_field,event->array_size,event->str);
    int i,j;
    for (i=0;i<event->array_size;i++){
        printf("%f ",event->array[i]);    
      }
      printf("\n");
      ev_send(cm,event->str,client_data);
}

static int
data_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    
//    event->array = (double*) malloc(event->array_size*sizeof(double));

    printf("I got %d, array_dim %d, string %s\n", event->integer_field,event->array_size,event->str);
    int i,j;
    for (i=0;i<event->array_size;i++){
        printf("%f ",event->array[i]);    
      }
      printf("\n");
      ev_send(cm,event->str,client_data);
}


enum status adios_ev_recv(char *component_name, int processID, simple_rec_ptr multi_array,char *ev_func){

}

/* this file is evpath/examples/derived_recv.c */
int main(int argc, char **argv)
{
    int i;
    CManager cm;
    EVstone meta_stone, data_stone ;
    char *meta_string_list, *data_string_list;
    char contact_data_addr[2048];
    FILE *contact = fopen("contact.txt","w");





    cm = CManager_create();
    CMlisten(cm);

    printf("executable name %s\n",argv[0]);

    array_data array1d;
    array1d.num_array_dim=1;
    array1d.local_dim = (int*) malloc(array1d.num_array_dim*sizeof(int));
    array1d.global_dim = (int*) malloc(array1d.num_array_dim*sizeof(int));
    array1d.start = (int*) malloc(array1d.num_array_dim*sizeof(int));
    array1d.end = (int*) malloc(array1d.num_array_dim*sizeof(int));
    array1d.total_local_size = 20*sizeof(int);
    array1d.addr = (char*) malloc (array1d.total_local_size);

    int temp[20];
    for (i=0;i<20;i++) temp[i]=i;

    array1d.local_dim[0]=20;
    array1d.global_dim[0]=20;
    array1d.start[0]= 0;
    array1d.end[0]= 19;

    array1d.addr=  (char*) temp[0];

// create data stone
    data_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, data_stone, metadata_format_list, data_handler, &array1d);
    data_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Contact data list \"%d:%s\"\n", data_stone, data_string_list);
     sprintf(contact_data_addr, "%d:%s", data_stone, meta_string_list);


 
//create meta data for array
    array_info.num_array_dim =1;
    array_info.dim_array_size = (int*) malloc(array_info.num_array_dim*sizeof(int));
    array_info.dim_proc_size = (int*) malloc(array_info.num_array_dim*sizeof(int));
    array_info.addr_size= 4096* array_info.num_array_dim;
    array_info.addr=  (char*) malloc(array_info.addr_size*sizeof(char));


    array_info.dim_array_size[0] = 20;
    array_info.dim_proc_size[0] = 1;
    sprintf(array_info.addr,"%s",contact_data_addr); 

// create meta stone
    meta_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, meta_stone, simple_format_list, request_handler, &array_info);
    meta_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Contact request list \"%d:%s\"\n", meta_stone, meta_string_list);
    fprintf(contact, "%d:%s", meta_stone, meta_string_list);


   

    fclose(contact);
    CMsleep(cm, 600);
}
