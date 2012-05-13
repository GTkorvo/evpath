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

    EVstone split_req_stone;
    EVaction split_req_action;

    EVstone split_handler_stone;
    EVaction split_handler_action;

    EVstone data_server_stone ;
    char *data_server_string_list ;
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


typedef struct _data_client{
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




	/* regardless of filtering or not, we'll need an output stone */
	output_stone = EValloc_stone(cm);
	contact_list = attr_list_from_string(string_list);
	//printf("This is the contact list   --------");
//	dump_attr_list(contact_list);
	EVassoc_bridge_action(cm, output_stone, contact_list, remote_stone);

  EVaction_add_split_target(cm, split_stone, split_action, output_stone);
    source = EVcreate_submit_handle(cm, split_stone, metadata_server_format_list);

    meta_data_server_ptr event = data;
    printf("Sending array to contact %s\n", event->addr);    
   	EVsubmit(source, data, NULL);
}

static int
data_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    printf("hello from data handler on server side \n");
    data_client_ptr array_req = vevent;
    printf("num_array_dim=%d, array_req.start=%d, array_req.end=%d, array_req.addr=%s\n", array_req->num_array_dim, array_req->start[0], array_req->end[0],array_req->addr);

    EVstone data_stone;
    EVstone client_data_stone;

    char data_string_list[2048];
    char contact_data_addr[2048];
    attr_list contact_data_list;
    EVsource array_data_source;

    array_data array;
    array.num_array_dim=1;
    array.start = (int*) malloc(array.num_array_dim*sizeof(int));
    array.end = (int*) malloc(array.num_array_dim*sizeof(int));
    array.global_dim = (int*) malloc(array.num_array_dim*sizeof(int));
    array.total_local_size = 20*sizeof(int);
    array.value = (char*) malloc(array.total_local_size);
    
  

    printf("server sending data back to client \n");
    // sending the data back to client
    // read the data connection file
    FILE *client_data_contact=fopen("client_data_contact","r");
    fscanf(client_data_contact,"%s", contact_data_addr);
    printf("contact for data %s\n",contact_data_addr);
    sscanf(array_req->addr,"%d:%s",&client_data_stone,data_string_list);

//  create a stone to send the data back to client
    data_stone = EValloc_stone(cm);
    contact_data_list = attr_list_from_string(data_string_list );
    EVassoc_bridge_action(cm,data_stone,contact_data_list,client_data_stone);
    EVaction_add_split_target(cm,split_handler_stone,split_handler_action,data_stone);


    array_data_source = EVcreate_submit_handle(cm,split_handler_stone,arraydata_format_list);
    EVsubmit(array_data_source,&array, NULL);
//    EVsubmit(array_data_source,client_data, NULL);
    printf("\n\n\n\n END \n\n\n\n");
    fclose(client_data_contact);


/*    ev_send_array(cm,array_req->addr, client_data);

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
*/

}



static int
request_handler(CManager cm, void *vevent, void *metadata_server, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    printf("hello from request handler on server side\n");   
//    event->array = (double*) malloc(event->array_size*sizeof(double));

    printf("I got %d, array_dim %d, string %s\n", event->integer_field,event->array_size,event->str);
    int i,j;
    for (i=0;i<event->array_size;i++){
        printf("%f ",event->array[i]);    
      }
      printf("\n");
//ev_send_metadata_server(cm,event->str,metadata_server);


//    sleep(2);    
     EVstone client_req_stone, meta_stone;

    char meta_string_list[2048];
    char contact_data_req[2048];
    attr_list contact_req_list;

    EVsource metadata_source;
//    FILE *client_req=fopen("client_req","r");
//    fscanf(client_req,"%s", contact_data_req);
//    printf("contact for req %s\n",contact_data_req);
//    sscanf(contact_data_req,"%d:%s",&client_req_stone,meta_string_list);
// sending the metadata back to client
// read the metadata connection file
    sscanf(event->str,"%d:%s",&client_req_stone,meta_string_list);
    printf("client_req_stone %d, meta_string_list %s\n",client_req_stone,meta_string_list); 
// create a stone to send metadata back to client
    meta_stone = EValloc_stone(cm);
    contact_req_list = attr_list_from_string(meta_string_list);
    EVassoc_bridge_action(cm,meta_stone,contact_req_list,client_req_stone);
    
    EVaction_add_split_target(cm,split_req_stone,split_req_action,meta_stone);
 
    metadata_source = EVcreate_submit_handle(cm,split_req_stone, metadata_server_format_list);
    meta_data_server data;

    data.num_array_dim=1;

    data.dim_array_size = (int*) malloc(data.num_array_dim*sizeof(int));
    data.dim_proc_size = (int*) malloc(data.num_array_dim*sizeof(int));

    data.dim_array_size[0]=20;
    data.dim_proc_size[0]=1;

    data.addr_size=2048;
    data.addr = (char*) malloc(data.addr_size*sizeof(char));
    sprintf(data.addr,"%d:%s",data_server_stone,data_server_string_list); 

    EVsubmit(metadata_source,&data, NULL);

}


/* this file is evpath/examples/derived_recv.c */
int main(int argc, char **argv)
{
    CManager cm;
    EVstone meta_stone, data_stone;
    EVstone client_req_stone, client_data_stone;

    char meta_string_list[2048], data_string_list[2048];
    char contact_data_addr[2048],contact_data_req[2048];
    attr_list contact_data_list, contact_req_list;
    EVsource metadata_source, array_data_source;

    array_data array;

    cm = CManager_create();
    CMlisten(cm);

    if (argc ==1) 
    {
    printf("server waiting for metadata from client\n");

    split_req_stone = EValloc_stone(cm);
    split_req_action = EVassoc_split_action(cm,split_req_stone,NULL);

    split_handler_stone = EValloc_stone(cm);
    split_handler_action = EVassoc_split_action(cm,split_handler_stone,NULL);

// create an action when receiving data requesting from client
    data_server_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm,data_server_stone, data_client_format_list, data_handler, NULL);
    data_server_string_list = attr_list_to_string(CMget_contact_list(cm));
    FILE *data_server = fopen("data_server","w");
    fprintf(data_server,"%d:%s",data_server_stone,data_server_string_list);
    printf("data server %d:%s\n",data_server_stone,data_server_string_list);
    fclose(data_server);



// create an action when receiving metadata requesting from client
    EVstone meta_server_stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, meta_server_stone, simple_format_list, request_handler, &array_info);
    char *meta_server_string_list = attr_list_to_string(CMget_contact_list(cm));
    FILE *metadata_server = fopen("metadata_server","w");
    fprintf(metadata_server,"%d:%s",meta_server_stone, meta_server_string_list);
    printf("metadata server %d:%s\n",meta_server_stone, meta_server_string_list);
    fclose(metadata_server);
 
   }
if (argc ==2)
{
    FILE *client_req=fopen("client_req","r");
    fscanf(client_req,"%s", contact_data_req);
    printf("contact for req %s\n",contact_data_req);
    sscanf(contact_data_req,"%d:%s",&client_req_stone,meta_string_list);
 
// create a stone to send metadata back to client
    meta_stone = EValloc_stone(cm);
    contact_req_list = attr_list_from_string(meta_string_list);
    EVassoc_bridge_action(cm,meta_stone,contact_req_list,client_req_stone);
 
    metadata_source = EVcreate_submit_handle(cm,meta_stone, metadata_server_format_list);
    meta_data_server data;
    EVsubmit(metadata_source,&data, NULL);


    fclose(client_req);



    printf("server sending data back to client \n");
    // sending the data back to client
    // read the data connection file
    FILE *client_data_contact=fopen("client_data_contact","r");
    fscanf(client_data_contact,"%s", contact_data_addr);
    printf("contact for data %s\n",contact_data_addr);
    sscanf(contact_data_addr,"%d:%s",&client_data_stone,data_string_list);

//  create a stone to send the data back to client
    data_stone = EValloc_stone(cm);
    contact_data_list = attr_list_from_string(data_string_list );
    EVassoc_bridge_action(cm,data_stone,contact_data_list,client_data_stone);
    array_data_source = EVcreate_submit_handle(cm,data_stone,arraydata_format_list);
    EVsubmit(array_data_source,&array, NULL);

    fclose(client_data_contact);

}

    CMsleep(cm, 600);
}
