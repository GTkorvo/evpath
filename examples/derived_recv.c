#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "evpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct _simple_rec {
    int integer_field;
    int array_size;
    double *array;
 
} simple_rec, *simple_rec_ptr;

static FMField simple_field_list[] =
{
    {"integer_field", "integer", sizeof(int), FMOffset(simple_rec_ptr, integer_field)},
    {"array_size", "integer", sizeof(int), FMOffset(simple_rec_ptr, array_size)},
    {"array", "double[array_size]", sizeof(double), FMOffset(simple_rec_ptr, array)},
    {NULL, NULL, 0, 0}
};
static FMStructDescRec simple_format_list[] =
{
    {"simple", simple_field_list, sizeof(simple_rec), NULL},
    {NULL, NULL}
};

static int
simple_handler(CManager cm, void *vevent, void *client_data, attr_list attrs)
{
    simple_rec_ptr event = vevent;
    
//    event->array = (double*) malloc(event->array_size*sizeof(double));

    printf("I got %d, array_dim %d\n", event->integer_field,event->array_size);
    int i,j;
    for (i=0;i<event->array_size;i++){
        printf("%f ",event->array[i]);    
      }
      printf("\n");
}

/* this file is evpath/examples/derived_recv.c */
int main(int argc, char **argv)
{
    CManager cm;
    EVstone stone;
    char *string_list, *filter_spec, *encoded_filter_spec;

    cm = CManager_create();
    CMlisten(cm);

    stone = EValloc_stone(cm);
    EVassoc_terminal_action(cm, stone, simple_format_list, simple_handler, NULL);
    string_list = attr_list_to_string(CMget_contact_list(cm));
    filter_spec = create_filter_action_spec(simple_format_list, 
					    "{ return input.integer_field % 2;}");
    encoded_filter_spec = atl_base64_encode(filter_spec, strlen(filter_spec) + 1);
    printf("Contact list \"%d:%s:%s\"\n", stone, string_list, encoded_filter_spec);
    free(filter_spec);
    free(encoded_filter_spec);
    CMsleep(cm, 600);
}
