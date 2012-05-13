#include "evpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _simple_rec {
    int integer_field;
    int array_size;
    double *array;
    char str[4096];
} simple_rec, *simple_rec_ptr;




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

typedef struct _meta_data_server{
    int num_array_dim;
    int *dim_array_size;
    int *dim_proc_size;
    int addr_size;
    char *addr;

} meta_data_server, *meta_data_server_ptr;

typedef struct _meta_client{
    int num_array_dim;
    int *start;
    int *end;
    char *addr;
} meta_client, *meta_client_ptr;

meta_data_server array_info;


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

enum status adios_ev_send(char* component_name, int processID, simple_rec_ptr multi_array, char *ev_func){


}


static int
metadata_server_recv(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
//receive metadata from publisher
      int i;
     meta_data_server_ptr event = vevent;
    
//    event->array = (double*) malloc(event->array_size*sizeof(double));
     printf("Hello from subcriber: num_array_dim = %d \n", event->num_array_dim);
    for (i=0;i < event->num_array_dim; i++){
      printf("i=%d, dim_array_size=%d,dim_proc_size=%d,contact=%s\n",i,event->dim_array_size[i],event->dim_proc_size[i],event->addr);

    }

//send request to receive array from publisher

}


enum status ev_send_request(CManager cm,char* contact_info, simple_rec_ptr data){
 
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
//      printf("string list %s\n, filter_spec = %s\n", string_list, filter_spec);

	    atl_base64_decode((unsigned char *)filter_spec, NULL);  /* decode in place */
	    printf("String list is %s\n", string_list);

      

	}



	/* regardless of filtering or not, we'll need an output stone */
	output_stone = EValloc_stone(cm);
	contact_list = attr_list_from_string(string_list);
//	printf("This is the contact list   --------");
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
    source = EVcreate_submit_handle(cm, split_stone, simple_format_list);

   	EVsubmit(source, data, NULL);
}

static int
data_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    printf("hello from data handler\n");
}
/* this file is evpath/examples/derived_send.c */
int main(int argc, char **argv)
{

    FILE *contact = fopen("contact.txt","r");

    EVstone meta_stone, data_stone ;
    char *meta_string_list, *data_string_list;
    char contact_data_addr[2048];

    CManager cm;
    simple_rec data;
    char *string_list;

   int i;
    printf("hello\n");
    data.array_size=10;
    data.array = (double*) malloc(data.array_size*sizeof(double));

    for (i=0;i< data.array_size;i++) data.array[i]=i;

    
    

    cm = CManager_create();
    CMlisten(cm);
    meta_stone = EValloc_stone(cm);

//create metadata stone
     EVassoc_terminal_action(cm, meta_stone, metadata_format_list, metadata_server_recv, NULL);
    string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Contact list \"%d:%s\"\n", meta_stone, string_list);

//create arraydata stone
    data_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, data_stone, metadata_format_list, data_handler, &array_info);
    data_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Contact data list \"%d:%s\"\n", data_stone, data_string_list);
     sprintf(contact_data_addr, "%d:%s", data_stone, meta_string_list);
 
/* this file is evpath/examples/derived_send.c */
//    for (i = 1; i < argc; i++) {
  char contact_info[4096];

   if (fscanf(contact, "%s", contact_info) != 1) {
	    printf("Bad argument \"%s\"\n", argv[i]);
	    exit(0);
	 }

   data.integer_field = 318;
//    data.str = "kraut";
    int meta_stone_id = (int) meta_stone;
    sprintf(data.str,"%d:%s", meta_stone_id,string_list);
	/* regardless of filtering or not, we'll need an output stone */
   for (i=0; i < 10; i++) {
      ev_send_request(cm, contact_info,&data);
    	data.integer_field++;

    }
   fclose(contact);

   CMsleep(cm, 600);


}
