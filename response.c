#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>
#include <ctype.h>

#include "evpath.h"
#include "cm_internal.h"
#include "ecl.h"
#include "libltdl/ltdl.h"

typedef enum {Response_Filter, Response_Transform, Response_Router, Response_Multiqueued} response_types;

struct terminal_spec {
    CMFormatList format_list;
    void *handler;
    void *client_data;
};

struct filter_spec {
    CMFormatList format_list;
    char *function;
    void *client_data;
    IOFormat reference_format;
};

struct transform_spec {
    CMFormatList in_format_list;
    CMFormatList out_format_list;
    char *function;
    void *client_data;
    IOFormat reference_input_format;
    IOFormat reference_output_format;
    EVsource source_handle;
    int output_base_struct_size;
};

struct multiqueued_spec {
    CMFormatList *struct_list;
    char *function;
    void *client_data;
    IOFormat *reference_input_format_list;
};

typedef struct response_spec {
    response_types response_type;
    union {
	struct terminal_spec term;
	struct filter_spec filter;
	struct transform_spec transform;
	struct multiqueued_spec multiqueued;
    }u;
} *handler_list;

struct filter_instance {
    int (*func_ptr)(void *, attr_list);
    ecl_code code;
    ecl_exec_context ec;
    void *client_data;
};

struct transform_instance {
    ecl_code code;
    ecl_exec_context ec;
    int out_size;
    void *client_data;
    IOFormat out_format;
};

typedef struct response_instance {
    response_types response_type;
    union {
	struct filter_instance filter;
	struct transform_instance transform;
    }u;
} *response_instance;



static char *
add_IOfieldlist_to_string(char *str, char *format_name, IOFieldList list)
{
    int index, field_count = 0;
    int len = strlen(str);
    char *tmp_str;
    len += strlen(format_name) + 5 + 35;
    str = realloc(str, len);
    while(list[field_count].field_name != NULL) field_count++;
    tmp_str = str + strlen(str); 
    sprintf(tmp_str, "IOFormat \"%s\" FieldCount %d\n", format_name, field_count);
    for (index = 0; index < field_count; index++) {
	len += strlen(list[index].field_name) +strlen(list[index].field_type) + 50;
	str = realloc(str, len);
	tmp_str = str + strlen(str); 
	sprintf(tmp_str, "    IOField \"%s\" \"%s\" %d %d\n",
		list[index].field_name, list[index].field_type,
		list[index].field_size, list[index].field_offset);
    }
    return str;
}

/*static char *
add_IOformat_to_string(char *str, IOFormat ioformat)
{
    return add_IOfieldlist_to_string(str, name_of_IOformat(ioformat),
				     field_list_of_IOformat(ioformat));
}*/

static char *
get_str(char *str, const char **name_p)
{
    int name_len = 0;
    char *name = malloc(1);
    while (*str != '"') {
	name = realloc(name, (name_len + 2));
	name[name_len++] = *(str++);
    }
    name[name_len] = 0;
    str++;
    *name_p = name;
    return str;
}
    
static char *
parse_IOformat_from_string(char *str, char **format_name, IOFieldList *list_p)
{
    char *name;
    IOFieldList list;
    *format_name = NULL;
    *list_p = NULL;
    if (strncmp(str, "IOFormat \"", 10) == 0) {
	int field_count;
	int index = 0;
	str += 10;
	str = get_str(str, (const char **)&name);
	str += strlen(" FieldCount ");
	if (sscanf(str, "%d", &field_count) == 1) {
	    while(isdigit((int)*str)) str++;
	}
	str++;
	list = malloc(sizeof(*list) * (field_count + 1));
	for (index = 0; index < field_count; index++) {
	    str += strlen("    IOField \"");
	    str = get_str(str, &(list[index].field_name));
	    str += 2;
	    str = get_str(str, &(list[index].field_type));
	    str++;
	    if (sscanf(str, "%d", &list[index].field_size) == 1) {
		while(isdigit((int)*str)) str++;
	    }
	    str++;
	    if (sscanf(str, "%d", &list[index].field_offset) == 1) {
		while(isdigit((int)*str)) str++;
	    }
	    str = strchr(str, '\n') + 1;
	}
	list[field_count].field_name = NULL;
	list[field_count].field_type = NULL;
	list[field_count].field_size = 0;
	list[field_count].field_offset = 0;
	*format_name = name;
	*list_p = list;
    }
    return str;
}

void *
install_response_handler(CManager cm, int stone_id, char *response_spec, 
			 void *local_data, IOFormat *ref_ptr)
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
/*	INT_EVassoc_terminal_action(cm, stone_id, list, local_data, NULL);*/
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
	function = malloc(strlen(str) + 1);
	strcpy(function, str);
	response->response_type = Response_Filter;
	response->u.filter.format_list = list;
	response->u.filter.function = function;
	response->u.filter.client_data = local_data;
	response->u.filter.reference_format = 
	    EVregister_format_set(cm, list, NULL);
	if (ref_ptr)
	    *ref_ptr = response->u.filter.reference_format;
	return (void*)response;
    }
    if (strncmp("Router Action", str, strlen("Router Action")) == 0) {
	struct response_spec *response = malloc(sizeof(struct response_spec));
	int format_count, i;
	char *function;
	CMFormatList list;
	str += strlen("Router Action") + 1;
	sscanf(str, "  Format Count %d\n", &format_count);
	str = strchr(str, '\n') + 1;
	list = malloc(sizeof(list[0]) * (format_count + 1));
	for (i=0; i < format_count; i++) {
	    str = parse_IOformat_from_string(str, &list[i].format_name,
					     &list[i].field_list);
	}
	function = malloc(strlen(str) + 1);
	strcpy(function, str);
	response->response_type = Response_Router;
	response->u.filter.format_list = list;
	response->u.filter.function = function;
	response->u.filter.client_data = local_data;
	response->u.filter.reference_format = 
	    EVregister_format_set(cm, list, NULL);
	if (ref_ptr)
	    *ref_ptr = response->u.filter.reference_format;
	return (void*)response;
    }
    if (strncmp("Transform Action", str, strlen("Transform Action")) == 0) {
	struct response_spec *response = malloc(sizeof(struct response_spec));
	int format_count, i;
	char *function;
	CMFormatList in_list, out_list;
	str += strlen("Transform Action") + 1;
	sscanf(str, "  Input Format Count %d\n", &format_count);
	str = strchr(str, '\n') + 1;
	in_list = malloc(sizeof(in_list[0]) * (format_count + 1));
	for (i=0; i < format_count; i++) {
	    str = parse_IOformat_from_string(str, &in_list[i].format_name,
					     &in_list[i].field_list);
	}
	in_list[format_count].format_name = NULL;
	in_list[format_count].field_list = NULL;
	if (sscanf(str, "  Output Format Count %d\n", &format_count) != 1) {
	    printf("output format parse failed\n");
	    return 0;
	}
	str = strchr(str, '\n') + 1;
	out_list = malloc(sizeof(out_list[0]) * (format_count + 1));
	for (i=0; i < format_count; i++) {
	    str = parse_IOformat_from_string(str, &out_list[i].format_name,
					     &out_list[i].field_list);
	}
	out_list[format_count].format_name = NULL;
	out_list[format_count].field_list = NULL;
	function = malloc(strlen(str) + 1);
	strcpy(function, str);
	response->response_type = Response_Transform;
	response->u.transform.in_format_list = in_list;
	response->u.transform.out_format_list = out_list;
	response->u.transform.function = function;
	response->u.transform.client_data = local_data;
	response->u.transform.reference_input_format = NULL;
	if (in_list[0].format_name != NULL) 
	    response->u.transform.reference_input_format = 
		EVregister_format_set(cm, in_list, NULL);
	if (ref_ptr)
	    *ref_ptr = response->u.transform.reference_input_format;
	if (out_list[0].format_name != NULL)
	    response->u.transform.reference_output_format = 
		EVregister_format_set(cm, out_list, NULL);
	response->u.transform.output_base_struct_size =
	    struct_size_field_list(out_list[0].field_list, sizeof(char*));
	return (void*)response;
    }
    if (strncmp("Multiqueued Action", str, strlen("Multiqueued Action")) == 0) {
	struct response_spec *response = malloc(sizeof(struct response_spec));
	int list_count, j;
	char *function;
	CMFormatList *struct_list;
	str += strlen("Multiqueued Action") + 1;
	sscanf(str, "  List Count %d\n", &list_count);
	str = strchr(str, '\n') + 1;
	struct_list = malloc(sizeof(struct_list[0]) * (list_count + 1));
	for (j = 0; j < list_count; j++) {
	    int format_count, i;
	    CMFormatList in_list;
	    scanf(str, "Next format   Subformat Count %d\n", &format_count);

	    in_list = malloc(sizeof(in_list[0]) * (format_count + 1));
	    for (i=0; i < format_count; i++) {
		str = parse_IOformat_from_string(str, &in_list[i].format_name,
						 &in_list[i].field_list);
	    }
	    in_list[format_count].format_name = NULL;
	    in_list[format_count].field_list = NULL;
	    struct_list[j] = in_list;
	}
	function = malloc(strlen(str) + 1);
	strcpy(function, str);
	response->response_type = Response_Multiqueued;
	response->u.multiqueued.struct_list = struct_list;
	response->u.multiqueued.function = function;
	response->u.multiqueued.client_data = local_data;
/*	response->u.multiqueued.reference_input_format = NULL;
	if (in_list[0].format_name != NULL) 
	    response->u.multiqueued.reference_input_format = 
		EVregister_format_set(cm, in_list, NULL);
	response->u.multiqueued.output_base_struct_size =
	struct_size_field_list(out_list[0].field_list, sizeof(char*));*/
	return (void*)response;
    }
    printf("Unparsed action : %s\n", str);
    return NULL;
}


char *
create_terminal_action_spec(CMFormatList format_list)
{
    int format_count = 0;
    int i;
    char *str;
    while(format_list[format_count].format_name != NULL) format_count++;
    str = malloc(50);
    sprintf(str, "Terminal Action   Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					format_list[i].field_list);
    }
    return str;
}

char *
INT_create_filter_action_spec(CMFormatList format_list, char *function)
{
    int format_count = 0;
    int i;
    char *str;
    while(format_list[format_count].format_name != NULL) format_count++;
    str = malloc(50);
    sprintf(str, "Filter Action   Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					format_list[i].field_list);
    }
    str = realloc(str, strlen(str) + strlen(function) + 1);
    strcpy(&str[strlen(str)], function);
    return str;
}

char *
INT_create_router_action_spec(CMFormatList format_list, char *function)
{
    int format_count = 0;
    int i;
    char *str;
    while(format_list[format_count].format_name != NULL) format_count++;
    str = malloc(50);
    sprintf(str, "Router Action   Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					format_list[i].field_list);
    }
    str = realloc(str, strlen(str) + strlen(function) + 1);
    strcpy(&str[strlen(str)], function);
    return str;
}

char *
INT_create_transform_action_spec(CMFormatList format_list, CMFormatList out_format_list, char *function)
{
    int format_count = 0;
    int i;
    char *str;
    while(format_list && format_list[format_count].format_name != NULL) 
	format_count++;
    str = malloc(50);
    sprintf(str, "Transform Action   Input Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					format_list[i].field_list);
    }

    format_count = 0;
    while(out_format_list[format_count].format_name != NULL) format_count++;
    str = realloc(str, strlen(str) + 30);
    sprintf(str + strlen(str), "  Output Format Count %d\n", format_count);

    for (i = 0 ; i < format_count; i++) {
	str = add_IOfieldlist_to_string(str, out_format_list[i].format_name,
					out_format_list[i].field_list);
    }
    str = realloc(str, strlen(str) + strlen(function) + 1);
    strcpy(&str[strlen(str)], function);
    return str;
}

extern char *
INT_create_multiqueued_action_spec(CMFormatList *input_format_lists, char *function)
{
    int list_count = 0;
    int l;
    char *str;
    while(input_format_lists && input_format_lists[list_count] != NULL) 
	list_count++;

    str = malloc(50);
    sprintf(str, "Multiqueued Action   List Count %d\n", list_count);

    for (l = 0; l < list_count; l++) {
	int format_count = 0, i;
	CMFormatList format_list = input_format_lists[l];
	while(format_list && format_list[format_count].format_name != NULL) 
	    format_count++;
	str = realloc(str, strlen(str) + 50);
	sprintf(str + strlen(str), "Next format   Subformat Count %d\n",
		format_count);
	for (i = 0 ; i < format_count; i++) {
	    str = add_IOfieldlist_to_string(str, format_list[i].format_name,
					    format_list[i].field_list);
	}
    }

    str = realloc(str, strlen(str) + strlen(function) + 1);
    strcpy(&str[strlen(str)], function);
    return str;
}

struct ev_state_data {
    CManager cm;
    struct _event_item *cur_event;
    int out_count;
    int *out_stones;
};

static int
filter_wrapper(CManager cm, struct _event_item *event, void *client_data,
	       attr_list attrs, int out_count, int *out_stones)
{
    response_instance instance = (response_instance)client_data;
    int ret;
    ecl_exec_context ec = instance->u.filter.ec;
    struct ev_state_data ev_state;

    ev_state.cm = cm;
    ev_state.cur_event = event;
    ev_state.out_count = out_count;
    ev_state.out_stones = out_stones;
    ecl_assoc_client_data(ec, 0x34567890, (long)&ev_state);

    ret = ((int(*)(ecl_exec_context, void *, attr_list))instance->u.filter.code->func)(ec, event->decoded_event, attrs);
    if (ret) {
	CMtrace_out(cm, EVerbose, "Filter function returned %d, submitting further to stone %d\n", ret, out_stones[0]);
	internal_path_submit(cm, out_stones[0], event);
    } else {
	CMtrace_out(cm, EVerbose, "Filter function returned %d, NOT submitting\n", ret);
    }
    return ret;
}
static int
router_wrapper(CManager cm, struct _event_item *event, void *client_data,
	       attr_list attrs, int out_count, int *out_stones)
{
    response_instance instance = (response_instance)client_data;
    int ret;
    if (instance->u.filter.func_ptr) {
	ret = ((int(*)(void *, attr_list))instance->u.filter.func_ptr)(event->decoded_event, attrs);
    } else {
	int (*func)(ecl_exec_context, void *, attr_list) = 
	    (int(*)(ecl_exec_context, void *, attr_list))instance->u.filter.code->func;
	ecl_exec_context ec = instance->u.filter.ec;
	struct ev_state_data ev_state;

	ev_state.cm = cm;
	ev_state.cur_event = event;
	ev_state.out_count = out_count;
	ev_state.out_stones = out_stones;
	ecl_assoc_client_data(ec, 0x34567890, (long)&ev_state);
	ret = (func)(ec, event->decoded_event, attrs);
    }
    if (ret >= 0) {
	if (ret >= out_count) {
	    CMtrace_out(cm, EVerbose, "Router function returned %d, larger than the number of associated outputs\n", ret);
	} else if (out_stones[ret] == -1) {
	    CMtrace_out(cm, EVerbose, "Router function returned %d, which has not been set with EVaction_set_output()\n", ret);
	} else {
	    CMtrace_out(cm, EVerbose, "Router function returned %d, submitting further to stone %d\n", ret);
	    internal_path_submit(cm, out_stones[ret], event);
	}
    } else {
	CMtrace_out(cm, EVerbose, "Router function returned %d, NOT submitting\n", ret);
    }
    return ret;
}

static void
transform_free_wrapper(void *data, void *free_data)
{
    response_instance instance = (response_instance)free_data;
    IOfree_var_rec_elements(iofile_of_IOformat(instance->u.transform.out_format),
			    instance->u.transform.out_format,
			    data);
}

static int
transform_wrapper(CManager cm, struct _event_item *event, void *client_data,
		  attr_list attrs, int out_count, int *out_stones)
{
    response_instance instance = (response_instance)client_data;
    int ret;
    void *out_event = malloc(instance->u.transform.out_size);
    int(*func)(ecl_exec_context, void *, void*, attr_list) = 
	(int(*)(ecl_exec_context, void *, void*, attr_list))instance->u.transform.code->func;
    ecl_exec_context ec = instance->u.transform.ec;
    struct ev_state_data ev_state;

    ev_state.cm = cm;
    ev_state.cur_event = event;
    ev_state.out_count = out_count;
    ev_state.out_stones = out_stones;
    ecl_assoc_client_data(ec, 0x34567890, (long)&ev_state);

    if (CMtrace_on(cm, EVerbose)) {
	printf("Input Transform Event is :\n");
	if (event->reference_format) {
	    dump_limited_unencoded_IOrecord(iofile_of_IOformat(event->reference_format),
					    event->reference_format,
					    event->decoded_event, 10240);
	} else {
	    printf("       ****  UNFORMATTED  ****\n");
	}
    }
    memset(out_event, 0, instance->u.transform.out_size);
    ret = func(ec, event->decoded_event, out_event, attrs);
    if (ret) {
	struct _EVSource s;
	if (CMtrace_on(cm, EVerbose)) {
	    IOFormat f = instance->u.transform.out_format;
	    printf(" Transform function returned %d, submitting further\n", ret);
	    dump_limited_unencoded_IOrecord(iofile_of_IOformat(f), f, 
					    out_event, 10240);
	}
	s.local_stone_id = out_stones[0];
	s.cm = cm;
	s.format = NULL;
	s.reference_format = instance->u.transform.out_format;
	s.free_func = transform_free_wrapper;
	s.free_data = instance;
	s.preencoded = 0;
	INT_EVsubmit(&s, out_event, NULL);
    } else {
	CMtrace_out(cm, EVerbose, "Filter function returned %d, NOT submitting\n", ret);
	transform_free_wrapper(out_event, instance);
    }
    return ret;
}

static response_instance
generate_filter_code(struct response_spec *mrd, stone_type stone, IOFormat format);

static IOFormat
localize_format(CManager cm, IOFormat format)
{
    IOFormat *formats = get_subformats_IOformat(format);
    int format_count = 0, i;
    CMFormatList list;
    while(formats[format_count] != NULL) format_count++;
    list = malloc(sizeof(list[0]) * (format_count + 1));
    for (i=0; i < format_count; i++) {
	list[format_count - i - 1].format_name = strdup(name_of_IOformat(formats[i]));
	list[format_count - i - 1].field_list = get_local_field_list(formats[i]);
    }
    free(formats);
    return EVregister_format_set(cm, list, NULL);
}

void
dump_mrd(void *mrdv)
{
    struct response_spec *mrd = (struct response_spec *) mrdv;
    switch (mrd->response_type) {
    case Response_Filter:
	printf("Reponse Filter, code is %s\n",
	       mrd->u.filter.function);
	break;
    case Response_Router:
	printf("Reponse Router, code is %s\n",
	       mrd->u.filter.function);
	break;
    case Response_Transform:
	printf("Reponse Transform, code is %s\n",
	       mrd->u.transform.function);
	break;
    case Response_Multiqueued:
	break;
    }
}

int
response_determination(CManager cm, stone_type stone, event_item *event)
{
    int nearest_proto_action = -1;
    int return_value = 0;
    IOFormat conversion_target_format = NULL;
    int i, format_count;
    IOFormat * formatList;
    IOcompat_formats older_format = NULL;

    formatList =
	(IOFormat *) malloc((stone->proto_action_count + 1) * sizeof(IOFormat));
    for (i = 0; i < stone->proto_action_count; i++) {
	formatList[i] = stone->proto_actions[i].reference_format;
    }
    format_count = stone->proto_action_count;
    formatList[format_count] = NULL;
    if (event->reference_format == NULL) {
	/* special case for unformatted input */
	int i;
	for (i=0 ; i < format_count ; i++) {
	    if (formatList[i] == NULL) nearest_proto_action = i;
	}
    } else {
	nearest_proto_action = IOformat_compat_cmp2(event->reference_format, 
						    formatList,
						    format_count,
						    &older_format);
    }
    free(formatList);
    if (nearest_proto_action != -1) {
	int action_generated = 0;
	if (stone->proto_actions[nearest_proto_action].action_type != Action_Immediate) {
	    response_cache_element *resp;
	    conversion_target_format = stone->proto_actions[nearest_proto_action].reference_format;
	    /* we'll install the conversion later, first map the response */
	    if (stone->response_cache_count == 0) {
		stone->response_cache = malloc(sizeof(stone->response_cache[0]));
	    } else {
		stone->response_cache = 
		    realloc(stone->response_cache,
			    (stone->response_cache_count + 1) * sizeof(stone->response_cache[0]));
	    }
	    resp = &stone->response_cache[stone->response_cache_count++];
	    proto_action *proto = &stone->proto_actions[nearest_proto_action];
	    resp->reference_format = conversion_target_format;
	    resp->proto_action_id = nearest_proto_action;
	    resp->action_type = proto->action_type;
	    resp->requires_decoded = proto->requires_decoded;
	} else {
	    /* must be immediate action */
	    response_instance instance;
	    struct response_spec *mrd;
	    mrd = 
		stone->proto_actions[nearest_proto_action].o.imm.mutable_response_data;
	    switch(mrd->response_type) {
	    case Response_Filter:
	    case Response_Router:
		if (event->event_encoded) {
		    conversion_target_format = 
			localize_format(cm, event->reference_format);
		} else {
		    conversion_target_format = event->reference_format;
		}
		break;
	    case Response_Transform:
		conversion_target_format = mrd->u.transform.reference_input_format;
		break;
	    case Response_Multiqueued:
		break;
	    }

	    instance = generate_filter_code(mrd, stone, conversion_target_format);
	    if (instance == 0) return 0;
	    action_generated++;
	    switch(mrd->response_type) {
	    case Response_Filter:
		INT_EVassoc_mutated_imm_action(cm, stone->local_id, nearest_proto_action, 
					       filter_wrapper, instance, 
					       conversion_target_format);
		break;
	    case Response_Router:
		INT_EVassoc_mutated_imm_action(cm, stone->local_id, nearest_proto_action, 
					       router_wrapper, instance, 
					       conversion_target_format);
		break;
	    case Response_Transform:
		INT_EVassoc_mutated_imm_action(cm, stone->local_id, nearest_proto_action, 
					       transform_wrapper, instance, 
					       conversion_target_format);
		break;
	    case Response_Multiqueued:
		break;
	    }
	    
	    return_value = 1;
	}
	if (conversion_target_format != NULL) {
	    if (event->event_encoded) {
		/* create a decode action */
		INT_EVassoc_conversion_action(cm, stone->local_id, 
					      conversion_target_format, 
					      event->reference_format);
/*	    printf(" Returning ");
	    dump_action(stone, a, "   ");*/
		return_value = 1;
	    } else {
		if (event->reference_format != conversion_target_format) {
		    printf("Bad things.  Conversion necessary, but event is not encoded\n");
		} else {
		    return_value = 1;
		}
	    }
	}
    }
    return return_value;
}

void
response_data_free(){}

static void
ecl_free_wrapper(void *data, void *free_data)
{
    event_item *event = (event_item *)free_data;
    IOfree_var_rec_elements(iofile_of_IOformat(event->reference_format),
			    event->reference_format,
			    data);
}

static void
internal_ecl_submit(ecl_exec_context ec, int port, void *data, void *type_info)
{
    struct ev_state_data *ev_state = (void*)ecl_get_client_data(ec, 0x34567890);
    CManager cm = ev_state->cm;
    event_path_data evp = ev_state->cm->evp;
    event_item *event;
    assert(CManager_locked(cm));
    if (data == ev_state->cur_event->decoded_event) {
	CMtrace_out(cm, EVerbose, 
		    "Internal ECL submit, resubmission of current input event to stone %d\n",
		    ev_state->out_stones[port]);
	internal_path_submit(ev_state->cm, ev_state->out_stones[port], ev_state->cur_event);
    } else {
	IOFormat event_format = NULL;
	CMtrace_out(cm, EVerbose, 
		    "Internal ECL submit, submission of new data to stone %d\n",
		    ev_state->out_stones[port]);
	if (event_format == NULL) {
	    event_format = EVregister_format_set(cm, (CMFormatList) type_info,
						 NULL);
	    if (event_format == NULL) {
		printf("Bad format information on submit\n");
		return;
	    }
	}
	event = get_free_event(evp);
	event->event_encoded = 0;
	event->decoded_event = data;
	event->reference_format = event_format;
	event->format = NULL;
/*	event->free_func = ecl_free_wrapper;*/
	event->free_func = NULL;
	event->free_arg = event;
	event->attrs = NULL;
	ecl_encode_event(cm, event);  /* map to memory we trust */
	event->event_encoded = 1;
	event->decoded_event = NULL;  /* lose old data */
	internal_path_submit(cm, ev_state->out_stones[port], event);
	return_event(cm->evp, event);
    }
}


static void
add_standard_routines(stone, context)
stone_type stone;
ecl_parse_context context;
{
    static char extern_string[] = "\
		int printf(string format, ...);\n\
		void *malloc(int size);\n\
		void free(void *pointer);\n\
		long lrand48();\n\
		double drand48();\n\
		void EVsubmit(ecl_exec_context ec, int port, void* d, ecl_type_spec dt);\n\
		attr_list stone_attrs;";

    static ecl_extern_entry externs[] = {
	{"printf", (void *) 0},
	{"malloc", (void*) 0},
	{"free", (void*) 0},
	{"lrand48", (void *) 0},
	{"drand48", (void *) 0},
	{"stone_attrs", (void *) 0},
	{"EVsubmit", (void *) 0},
	{(void *) 0, (void *) 0}
    };
    /* 
     * some compilers think it isn't a static initialization to put this
     * in the structure above, so do it explicitly.
     */
    externs[0].extern_value = (void *) (long) printf;
    externs[1].extern_value = (void *) (long) malloc;
    externs[2].extern_value = (void *) (long) free;
    externs[3].extern_value = (void *) (long) lrand48;
    externs[4].extern_value = (void *) (long) drand48;
    externs[5].extern_value = (void *) (long) &stone->stone_attrs;
    externs[6].extern_value = (void *) (long) &internal_ecl_submit;

    ecl_assoc_externs(context, externs);
    ecl_parse_for_context(extern_string, context);
}

static void
add_param(ecl_parse_context parse_context, char *name, int param_num,
	  IOFormat format)
{
    IOFormat *formats = get_subformats_IOformat(format);
    char *tname = malloc(strlen(name) + strlen("_type") +1);
    int i = 0;
    sm_ref type, param;
    while (formats[i] != NULL) {
	sm_ref typ;
	IOFieldList fl = get_local_field_list(formats[i]);
	/* step through input formats */
	typ = ecl_build_type_node(name_of_IOformat(formats[i]), fl);
	ecl_add_decl_to_parse_context(name_of_IOformat(formats[i]), typ,
				      parse_context);
	i++;
    }
    sprintf(tname, "%s_type", name);
    type = ecl_build_type_node(tname, get_local_field_list(formats[i-1]));
    ecl_add_decl_to_parse_context(tname, type, parse_context);

    param = ecl_build_param_node(name, type, param_num);

    ecl_add_decl_to_parse_context(name, param, parse_context);
    free(formats);
}

#if 0
static void
add_param_list(ecl_parse_context parse_context, char *name, int param_num,
	  CMFormatList list)
{
    char *tname = malloc(strlen(name) + strlen("_type") +1);
    sm_ref type, param;
    int i = 0;
    while (list[i].format_name != NULL) {
	sm_ref typ;
	/* step through input formats */
	typ = ecl_build_type_node(list[i].format_name,
				  list[i].field_list);
	ecl_add_decl_to_parse_context(list[i].format_name, typ,
				      parse_context);
	i++;
    }
    sprintf(tname, "%s_type", name);
    type = ecl_build_type_node(tname, list[i-1].field_list);
    ecl_add_decl_to_parse_context(tname, type, parse_context);

    param = ecl_build_param_node(name, type, 0);

    ecl_add_decl_to_parse_context(name, param, parse_context);
}
#endif

static int
check_filter_string(filter)
char *filter;
{

    if (filter[0] == 'd' && filter[1] == 'l' && filter[2] == 'l' && filter[3] == ':') {
    return 1;
    }
    return 0;
}

static char *
extract_dll_path(filter)
char *filter;
{
    char *copy = strdup(filter);
    char *temp;
    char *path;


    temp = strtok(copy, ":");
    if (strcmp(temp, "dll")) {
    return NULL;
    }
    temp = strtok(NULL, ":");

    if (temp == NULL)
    return NULL;

    path = strdup(temp);

    return path;
}

static char *
extract_symbol_name(filter)
char *filter;
{

    char *copy = strdup(filter);
    char *temp;
    char *symbol;

    temp = strtok(copy, ":");
    if (strcmp(temp, "dll")) {
    return NULL;
    }
    temp = strtok(NULL, ":");
    temp = strtok(NULL, ":");

    if (temp == NULL)
    return NULL;

    symbol = strdup(temp);

    return symbol;
}

static void*
load_dll_symbol(path, symbol_name)
char *path;
char *symbol_name;
{
    lt_dlhandle handle;

	if(lt_dlinit() != 0) {
		fprintf(stderr, "Error in init: %s\n", lt_dlerror());
		return NULL;
	}
    handle = lt_dlopen(path);
    if (!handle) {
    	fprintf(stderr, "failed on dll open %s\n", lt_dlerror());
	    return NULL;
    }
    return lt_dlsym(handle, symbol_name);
}


static response_instance
generate_filter_code(mrd, stone, format)
struct response_spec *mrd;
stone_type stone;
IOFormat format;
{
    response_instance instance = malloc(sizeof(*instance));

    ecl_code code;
    ecl_parse_context parse_context = new_ecl_parse_context();
    /*    sm_ref conn_info_data_type, conn_info_param;*/

    memset(instance, 0, sizeof(*instance));
    add_standard_routines(stone, parse_context);

    switch (mrd->response_type) {
    case Response_Filter:
    case Response_Router:
    case Response_Transform:
	ecl_add_param("ec", "ecl_exec_context", 0, parse_context);
	if (format) {
	    add_param(parse_context, "input", 1, format);
	} else {
	    ecl_add_param("input", "int", 1, parse_context);
	}
	if (mrd->response_type == Response_Transform) {
	    add_param(parse_context, "output", 2, 
		      mrd->u.transform.reference_output_format);
	}
	break;
    case Response_Multiqueued:
	break;
    }
	    
/*    conn_info_data_type = ecl_build_type_node("output_conn_info_type",
					      output_conn_field_list);
    ecl_add_decl_to_parse_context("output_conn_info_type", 
				  conn_info_data_type, parse_context);
    conn_info_param = ecl_build_param_node("output_conn_info",
					   conn_info_data_type, 3);
    ecl_add_decl_to_parse_context("output_conn_info", conn_info_param,
				  parse_context);
*/
    switch(mrd->response_type) {
    case Response_Filter:
    case Response_Router:

	if (check_filter_string(mrd->u.filter.function)) {
	    /* it is a dll */
	    char *path = NULL;
	    char *symbol_name = NULL;
	    
	    path = extract_dll_path(mrd->u.filter.function);
	    symbol_name = extract_symbol_name(mrd->u.filter.function);
	    instance->u.filter.func_ptr = (int(*)(void*,attr_list)) load_dll_symbol(path, symbol_name);
	    instance->u.filter.code = NULL;
	} else {
	    code = ecl_code_gen(mrd->u.filter.function, parse_context);
	    instance->response_type = mrd->response_type;
	    instance->u.filter.code = code;
	    if (code)
		instance->u.filter.ec = ecl_create_exec_context(code);
	    
	    instance->u.filter.func_ptr = NULL;
	}
	break;
    case Response_Transform:
	code = ecl_code_gen(mrd->u.transform.function, parse_context);
	instance->response_type = Response_Transform;
	instance->u.transform.code = code;
	if (code)
	    instance->u.transform.ec = ecl_create_exec_context(code);
	instance->u.transform.out_size = 
	    mrd->u.transform.output_base_struct_size;
	instance->u.transform.out_format = 
	    mrd->u.transform.reference_output_format;
	break;
    case Response_Multiqueued:
	break;
    }
    ecl_free_parse_context(parse_context);

    return instance;
}

