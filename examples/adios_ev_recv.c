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

static FMField metadata_server_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(meta_data_server_ptr, num_array_dim)},
    {"dim_array_size", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_server_ptr, dim_array_size)},
    {"dim_proc_size", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_server_ptr, dim_proc_size)},
     {"addr_size", "integer", sizeof(int), FMOffset(meta_data_server_ptr, addr_size)},
   {"str", "char[addr_size]", sizeof(char), FMOffset(meta_data_server_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec metadata_server_format_list[] =
{
    {"meta_server", metadata_server_field_list, sizeof(meta_data_server), NULL},
    {NULL, NULL}
};


typedef struct _meta_data_client{
    int num_array_dim;
    int *start;
    int *end;
    int length;
    char *addr;

} meta_data_client, *meta_data_client_ptr;

static FMField metadata_client_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(meta_data_client_ptr, num_array_dim)},
    {"start", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_client_ptr, start)},
    {"end", "integer[num_array_dim]", sizeof(int), FMOffset(meta_data_client_ptr, end)},
   {"length", "integer", sizeof(int), FMOffset(meta_data_client_ptr, length)},
   {"addr", "char[length]", sizeof(char), FMOffset(meta_data_client_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec metadata_client_format_list[] =
{
    {"meta_client", metadata_client_field_list, sizeof(meta_data_client), NULL},
    {NULL, NULL}
};

typedef struct _array_data{
    int num_array_dim;
    int *start;
    int *end;
    int *global_dim;
    int total_local_size;
    char *value;
} array_data, *array_data_ptr;

static FMField arraydata_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(array_data_ptr, num_array_dim)},
    {"start", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, start)},
    {"end", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, end)},
    {"global_dim", "integer[num_array_dim]", sizeof(int), FMOffset(array_data_ptr, global_dim)},
     {"total_local_size", "integer", sizeof(int), FMOffset(array_data_ptr, total_local_size)},
     {"value", "char[total_local_size]", sizeof(char), FMOffset(array_data_ptr, value)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec arraydata_format_list[] =
{
    {"array", arraydata_field_list, sizeof(array_data), NULL},
    {NULL, NULL}
};




enum status {success,failure} status_t;

enum status ev_send_metadata_server(CManager cm,char* contact_info, void* data){
 
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
    source = EVcreate_submit_handle(cm, split_stone, metadata_server_format_list);

    meta_data_server_ptr event = data;
    printf("Sending array to contact %s\n", event->addr);    
   	EVsubmit(source, data, NULL);
}

enum status ev_send_array(CManager cm,char* contact_info, void* data){
 
   int i;
  EVsource source;
	EVstone remote_stone, output_stone;
	attr_list contact_list;
  EVstone split_stone;
  EVaction split_action;

	char *filter_spec;

	char string_list[2048];
 
    printf("going to send contact %s\n", contact_info);    
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
    source = EVcreate_submit_handle(cm, split_stone, arraydata_format_list);

    meta_data_server_ptr event = data;
   	EVsubmit(source, data, NULL);
}


static int
request_handler(CManager cm, void *vevent, void *metadata_server, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    
//    event->array = (double*) malloc(event->array_size*sizeof(double));

    printf("I got %d, array_dim %d, string %s\n", event->integer_field,event->array_size,event->str);
    int i,j;
    for (i=0;i<event->array_size;i++){
        printf("%f ",event->array[i]);    
      }
      printf("\n");
      ev_send_metadata_server(cm,event->str,metadata_server);
}

static int
data_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    printf("hello \n");
    meta_data_client_ptr array_req = vevent;
    printf("num_array_dim=%d, array_req.start=%d, array_req.end=%d, array_req.addr=%s\n", array_req->num_array_dim, array_req->start[0], array_req->end[0],array_req->addr);
//    ev_send_array(cm,array_req->addr, client_data);

    EVstone data_stone, client_data_stone;

    EVsource array_data_source;

    char data_string_list[2048], contact_data_addr[2048];
    attr_list contact_data_list;

    sscanf(array_req->addr,"%d:%s",&client_data_stone,data_string_list);
    data_stone = EValloc_stone(cm);
    contact_data_list = attr_list_from_string(data_string_list );
    EVassoc_bridge_action(cm,data_stone,contact_data_list,client_data_stone);
    array_data_source = EVcreate_submit_handle(cm,data_stone,arraydata_format_list);
    EVsubmit(array_data_source,client_data, NULL);


}


enum status adios_ev_recv(char *component_name, int processID, simple_rec_ptr multi_array,char *ev_func){

}

/* this file is evpath/examples/derived_recv.c */
int main(int argc, char **argv)
{
    CManager cm;
    EVstone meta_stone, data_stone ;
    char *meta_string_list, *data_string_list;
    char contact_data_addr[2048];
    FILE *contact = fopen("contact.txt","w");


    array_data array;
    array.num_array_dim=1;



    cm = CManager_create();
    CMlisten(cm);

    printf("executable name %s\n",argv[0]);

// create meta stone
    meta_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, meta_stone, simple_format_list, request_handler, &array_info);
    meta_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Contact request list \"%d:%s\"\n", meta_stone, meta_string_list);
    fprintf(contact, "%d:%s", meta_stone, meta_string_list);

// create data stone
    data_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, data_stone, metadata_client_format_list, data_handler, &array);
    data_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Contact data list \"%d:%s\"\n", data_stone, data_string_list);
     sprintf(contact_data_addr, "%d:%s", data_stone, data_string_list);
 

    array_info.num_array_dim =1;
    array_info.dim_array_size = (int*) malloc(array_info.num_array_dim*sizeof(int));
    array_info.dim_proc_size = (int*) malloc(array_info.num_array_dim*sizeof(int));
    array_info.addr_size= 4096* array_info.num_array_dim;
    array_info.addr=  (char*) malloc(array_info.addr_size*sizeof(char));


    array_info.dim_array_size[0] = 10;
    array_info.dim_proc_size[0] = 1;
    sprintf(array_info.addr,"%s",contact_data_addr); 
   

    fclose(contact);
    CMsleep(cm, 600);
}
