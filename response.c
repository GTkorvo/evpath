#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>

#include "evpath.h"
#include "cm_internal.h"

typedef enum {Response_Terminal, Response_Filter} response_types;

struct terminal_spec {
    CMFormatList format_list;
    void *handler;
    void *client_data;
};

struct filter_spec {
    CMFormatList format_list;
    char *function;
    void *client_data;
};

typedef struct response_spec {
    response_types response_type;
    union {
	struct terminal_spec term;
	struct filter_spec filter;
    }u;
} *handler_list;



void *
install_response_handler(CManager cm, int stone_id, char *response_spec, 
			 void *local_data)
{
    char *str = response_spec;
    if (strncmp("Terminal Action", str, strlen("Terminal Action")) == 0) {
	int format_count, i;
	CMFormatList list;
	str += strlen("Terminal Action") + 1;
	sscanf(str, "  Format Count %d\n", &format_count);
	str = strchr(str, '\n') + 1;
	list = malloc(sizeof(list[0]) * (format_count + 1));
	for (i=0; i < format_count; i++) {
	    str = parse_IOformat_from_string(str, &list[i].format_name,
					     &list[i].field_list);
	}
/*	EVassoc_terminal_action(cm, stone_id, list, local_data, NULL);*/
    }
    if (strncmp("Filter Action", str, strlen("Filter Action")) == 0) {
	struct response_spec *response = malloc(sizeof(struct response_spec));
	int format_count, i;
	char *function;
	CMFormatList list;
	str += strlen("Filter Action") + 1;
	sscanf(str, "  Format Count %d\n", &format_count);
	str = strchr(str, '\n') + 1;
	list = malloc(sizeof(list[0]) * (format_count + 1));
	for (i=0; i < format_count; i++) {
	    str = parse_IOformat_from_string(str, &list[i].format_name,
					     &list[i].field_list);
	}
	str = strchr(str, '\n') + 1;
	function = malloc(strlen(str) + 1);
	strcpy(function, str);
	response->response_type = Response_Filter;
	response->u.filter.format_list = list;
	response->u.filter.function = function;
	response->u.filter.client_data = local_data;
    }
}


char *
create_terminal_action_spec(CMFormatList format_list)
{
    int format_count = 0;
    int i;
    while(format_list[format_count].format_name != NULL) format_count++;
    char *str = malloc(50);
    sprintf(str, "Terminal Action   Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					format_list[i].field_list);
    }
    return str;
}

char *
create_filter_action_spec(CMFormatList format_list, char *function)
{
    int format_count = 0;
    int i;
    while(format_list[format_count].format_name != NULL) format_count++;
    char *str = malloc(50);
    sprintf(str, "Terminal Action   Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					format_list[i].field_list);
    }
    str = realloc(str, strlen(str) + strlen(function) + 1);
    strcpy(&str[strlen(str)], function);
    return str;
}

int
response_determination(CManager cm, stone_type stone, event_item *event)
{
    int nearest_proto_action;
/*    printf("IN RESPONSE DETERMINATION\n");*/
    if (stone->proto_action_count > 0) {
	int i;
	IOFormat * formatList;
	IOcompat_formats older_format = NULL;
	formatList =
	    (IOFormat *) malloc((stone->proto_action_count + 1) * sizeof(IOFormat));
	for (i = 0; i < stone->proto_action_count; i++) {
	    formatList[i] = stone->proto_actions[i].reference_format;
	}
	formatList[stone->proto_action_count] = NULL;
	nearest_proto_action = IOformat_compat_cmp(event->reference_format, 
						   formatList,
						   stone->proto_action_count,
						   &older_format);
	free(formatList);
    }

    /* This format is to be bound with action nearest_proto_action */
    if (1) {
	if (event->event_encoded) {
	    /* create a decode action */
	    EVstone_install_conversion_action(cm, stone->local_id, 
					      stone->proto_actions[nearest_proto_action].reference_format, 
					      event->reference_format);
/*	    printf(" Returning ");
	    dump_action(stone, a, "   ");*/
	    return 1;
	}
    }
    return 0;
}

int
response_data_free(){}

