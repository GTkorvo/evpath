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

} data_client, *data_client_ptr;

static FMField data_client_field_list[] =
{
    {"num_array_dim", "integer", sizeof(int), FMOffset(data_client_ptr, num_array_dim)},
    {"start", "integer[num_array_dim]", sizeof(int), FMOffset(data_client_ptr, start)},
    {"end", "integer[num_array_dim]", sizeof(int), FMOffset(data_client_ptr, end)},
     {"length", "integer", sizeof(int), FMOffset(data_client_ptr, length)},
  {"addr", "char[length]", sizeof(char), FMOffset(data_client_ptr, addr)},
    {NULL, NULL, 0, 0}
};


static FMStructDescRec data_client_format_list[] =
{
    {"meta_client", data_client_field_list, sizeof(data_client), NULL},
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

enum status adios_ev_send(char* component_name, int processID, simple_rec_ptr multi_array, char *ev_func){


}


enum status ev_send_array_request(CManager cm,char* contact_info, data_client_ptr data){
 
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




	/* regardless of filtering or not, we'll need an output stone */
	output_stone = EValloc_stone(cm);
	contact_list = attr_list_from_string(string_list);
//	printf("This is the contact list   --------");
//	dump_attr_list(contact_list);
	EVassoc_bridge_action(cm, output_stone, contact_list, remote_stone);

	    EVaction_add_split_target(cm, split_stone, split_action, output_stone);
    source = EVcreate_submit_handle(cm, split_stone, data_client_format_list);

   	EVsubmit(source, data, NULL);
}

static int array_handler(CManager cm, void *vevent, void *array_req, attr_list attrs)
{
    printf("hello from client \n"); 
}

static int
metadata_request(CManager cm, void *vevent, void *array_req, attr_list attrs)
{
      int i;
     meta_data_server_ptr event = vevent;
    
//    event->array = (double*) malloc(event->array_size*sizeof(double));
     printf("Hello from subcriber: num_array_dim = %d, data server addr %s \n", event->num_array_dim, event->addr);
//      ev_send_array_request(cm, event->addr ,array_req);

//create an action when receiving data from server
    EVstone data_stone = EValloc_stone(cm);

     EVassoc_terminal_action(cm, data_stone, arraydata_format_list, array_handler,NULL);
    char* data_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Data contact list \"%d:%s\"\n", data_stone, data_string_list);
    FILE *client_data_contact=fopen("client_data_contact","w");

    fprintf(client_data_contact,"%d:%s", data_stone, data_string_list);

    fclose(client_data_contact);


 // create a stone to send data request to server
    // read the address of the data handling server
    char contact_data_server[4096], data_server_string_list[4096];
    EVstone server_data_stone;
    attr_list contact_data_server_list;

    sscanf(event->addr,"%d:%s",&server_data_stone,data_server_string_list);

    EVstone split_stone;
    EVaction split_action;
    split_stone = EValloc_stone(cm);
    split_action = EVassoc_split_action(cm,split_stone,NULL);


    // create a stone to send data to data handling server
    EVstone data_client_stone = EValloc_stone(cm);
    contact_data_server_list = attr_list_from_string(data_server_string_list);
    EVassoc_bridge_action(cm,data_client_stone,contact_data_server_list,server_data_stone);
    EVaction_add_split_target(cm,split_stone,split_action,data_client_stone);
    EVsource data_source = EVcreate_submit_handle(cm, split_stone, data_client_format_list);


    data_client client_data;
    client_data.length = 4096;
    client_data.addr = (char*) malloc(client_data.length*sizeof(char));
    client_data.num_array_dim=1;
    client_data.start = (int*) malloc(client_data.num_array_dim*sizeof(int));
    client_data.end = (int*) malloc(client_data.num_array_dim*sizeof(int));
    sprintf(client_data.addr,"%d:%s",data_stone,data_string_list);

    client_data.start[0]=5;
    client_data.end[0]=10;
    
    EVsubmit(data_source,&client_data,NULL);

/*    for (i=0;i < event->num_array_dim; i++){
      printf("i=%d, dim_array_size=%d,dim_proc_size=%d,contact=%s\n",i,event->dim_array_size[i],event->dim_proc_size[i],event->addr);
      ev_send_array_request(cm, event->addr ,array_req);

    }
*/

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

    int i;
    printf("hello\n");
    data.array_size=10;
    data.array = (double*) malloc(data.array_size*sizeof(double));

    for (i=0;i< data.array_size;i++) data.array[i]=i;

    data_client array_req;

    array_data array;
    char temp_addr[2048];


    cm = CManager_create();
    CMlisten(cm);
if (argc==1)
{
    printf("client waiting for metadata from server\n");
//create an action when receiving metadata from server

    meta_stone = EValloc_stone(cm);
     EVassoc_terminal_action(cm, meta_stone, metadata_server_format_list, metadata_request, &array_req);
    meta_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Request contact list \"%d:%s\"\n", meta_stone, meta_string_list);
    FILE *client_req=fopen("client_req","w");
    fprintf(client_req, "%d:%s", meta_stone, meta_string_list);
    fclose(client_req);
 


    printf("client sending data to server\n");
// create a stone to send metadata request to server
    char contact_metadata_server[4096], metadata_server_string_list[4096];
    EVstone server_metadata_stone;
    attr_list contact_metaserver_list;

    //read address of the metadata handling server
    char *metadata_server_stone;
    FILE *metadata_server= fopen("metadata_server","r");
    fscanf(metadata_server,"%s",contact_metadata_server);
    printf("contact for metadata server %s \n", contact_metadata_server);
    sscanf(contact_metadata_server,"%d:%s",&server_metadata_stone,metadata_server_string_list);
    fclose(metadata_server);
  
    // create a stone to send request to metadata server
    EVstone metadata_client_stone = EValloc_stone(cm);
    attr_list contact_metadata_server_list = attr_list_from_string(metadata_server_string_list);
    EVassoc_bridge_action(cm, metadata_client_stone, contact_metadata_server_list, server_metadata_stone);
    EVsource metadata_source = EVcreate_submit_handle(cm,metadata_client_stone,simple_format_list);
  
    simple_rec client_request;
    client_request.integer_field=1;
    client_request.array_size=10;
    client_request.array = (double*) malloc(client_request.array_size*sizeof(double));
    for (i=0;i<client_request.array_size;i++)
    client_request.array[i]=i;
    sprintf(client_request.str,"%d:%s",meta_stone,meta_string_list);

    EVsubmit(metadata_source, &client_request, NULL);
/*

// create a stone to send data request to server
    // read the address of the data handling server
    char contact_data_server[4096], data_server_string_list[4096];
    EVstone server_data_stone;
    attr_list contact_data_server_list;

    FILE *data_server= fopen("data_server","r");
    fscanf(data_server,"%s",contact_data_server);
    printf("contact data server %s \n",contact_data_server);
    sscanf(contact_data_server,"%d:%s",&server_data_stone,data_server_string_list);
    fclose(data_server); 

    // create a stone to send data to data handling server
    EVstone data_client_stone = EValloc_stone(cm);
    contact_data_server_list = attr_list_from_string(data_server_string_list);
    EVassoc_bridge_action(cm,data_client_stone,contact_data_server_list,server_data_stone);
    EVsource data_source = EVcreate_submit_handle(cm, data_client_stone, data_client_format_list);
    data_client client_data;
    EVsubmit(data_source,&client_data,NULL);
*/
}
if (argc==2)
{

//create an action when receiving metadata from server

     meta_stone = EValloc_stone(cm);
     EVassoc_terminal_action(cm, meta_stone, metadata_server_format_list, metadata_request, &array_req);
    meta_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Request contact list \"%d:%s\"\n", meta_stone, meta_string_list);
    FILE *client_req=fopen("client_req","w");
    fprintf(client_req, "%d:%s", meta_stone, meta_string_list);
    fclose(client_req);
 


//create an action when receiving data from server
    data_stone = EValloc_stone(cm);

     EVassoc_terminal_action(cm, data_stone, arraydata_format_list, array_handler, &array);
    data_string_list = attr_list_to_string(CMget_contact_list(cm));
    printf("Data contact list \"%d:%s\"\n", data_stone, data_string_list);
    FILE *client_data_contact=fopen("client_data_contact","w");

    fprintf(client_data_contact,"%d:%s", data_stone, data_string_list);

    fclose(client_data_contact);
} 
   CMsleep(cm, 600);


}
