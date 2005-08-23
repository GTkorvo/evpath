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

typedef enum {Response_Filter, Response_Transform} response_types;

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

typedef struct response_spec {
    response_types response_type;
    union {
	struct terminal_spec term;
	struct filter_spec filter;
	struct transform_spec transform;
    }u;
} *handler_list;

struct filter_instance {
    ecl_code code;
    void *client_data;
};

struct transform_instance {
    ecl_code code;
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
	response->u.transform.reference_input_format = 
	    EVregister_format_set(cm, in_list, NULL);
	response->u.transform.reference_output_format = 
	    EVregister_format_set(cm, out_list, NULL);
	response->u.transform.output_base_struct_size =
	    struct_size_field_list(out_list[0].field_list, sizeof(char*));
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
INT_create_transform_action_spec(CMFormatList format_list, CMFormatList out_format_list, char *function)
{
    int format_count = 0;
    int i;
    char *str;
    while(format_list[format_count].format_name != NULL) format_count++;
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

static int
filter_wrapper(CManager cm, struct _event_item *event, void *client_data,
	       attr_list attrs, int *out_stones)
{
    response_instance instance = (response_instance)client_data;
    int ret;
    ret = ((int(*)(void *, attr_list))instance->u.filter.code->func)(event->decoded_event, attrs);
    if (ret) {
	CMtrace_out(cm, EVerbose, "Filter function returned %d, submitting further\n", ret);
	internal_path_submit(cm, out_stones[0], event);
    } else {
	CMtrace_out(cm, EVerbose, "Filter function returned %d, NOT submitting\n", ret);
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
		  attr_list attrs, int *out_stones)
{
    response_instance instance = (response_instance)client_data;
    int ret;
    void *out_event = malloc(instance->u.transform.out_size);
    int(*func)(void *, void*, attr_list) = 
	(int(*)(void *, void*, attr_list))instance->u.transform.code->func;
    if (CMtrace_on(cm, EVerbose)) {
	printf("Input Transform Event is :\n");
	dump_limited_unencoded_IOrecord(iofile_of_IOformat(event->reference_format),
					event->reference_format,
					event->decoded_event, 10240);
    }
    ret = func(event->decoded_event, out_event, attrs);
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
	INT_EVsubmit(&s, out_event, NULL);
    } else {
	CMtrace_out(cm, EVerbose, "Filter function returned %d, NOT submitting\n", ret);
	transform_free_wrapper(out_event, instance);
    }
    return ret;
}

static response_instance
generate_filter_code(struct response_spec *mrd, IOFormat format);

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
    case Response_Transform:
	printf("Reponse Transform, code is %s\n",
	       mrd->u.transform.function);
	break;
    }
}

int
response_determination(CManager cm, stone_type stone, event_item *event)
{
    int nearest_proto_action = -1;
    int return_value = 0;
    IOFormat conversion_target_format = NULL;
    int i, format_count, action_num = -1;
    IOFormat * formatList;
    IOcompat_formats older_format = NULL;

    formatList =
	(IOFormat *) malloc((stone->proto_action_count + stone->action_count + 1) * sizeof(IOFormat));
    for (i = 0; i < stone->proto_action_count; i++) {
	formatList[i] = stone->proto_actions[i].reference_format;
    }
    format_count = stone->proto_action_count;
    for (i = 0; i < stone->action_count; i++) {
	if (stone->actions[i].action_type == Action_Immediate) {
	    struct response_spec *mrd = 
		stone->actions[i].o.imm.mutable_response_data;
	    switch(mrd->response_type) {
	    case Response_Filter:
		formatList[format_count++] = mrd->u.filter.reference_format;
		break;
	    case Response_Transform:
		formatList[format_count++] = mrd->u.transform.reference_input_format;
		break;
	    }
	}
    }
    formatList[format_count] = NULL;
    nearest_proto_action = IOformat_compat_cmp2(event->reference_format, 
						formatList,
						format_count,
						&older_format);
    free(formatList);
    if (nearest_proto_action != -1) {
	if (nearest_proto_action < stone->proto_action_count) {
	    conversion_target_format = stone->proto_actions[nearest_proto_action].reference_format;
	} else {
	    if (nearest_proto_action != -1) {
		/* must be immediate action */
		int format_count = stone->proto_action_count;
		response_instance instance;
		struct response_spec *mrd;
		for (i = 0; i < stone->action_count; i++) {
		    if (stone->actions[i].action_type == Action_Immediate) {
			if (format_count == nearest_proto_action) {
			    mrd = 
				stone->actions[i].o.imm.mutable_response_data;
			    switch(mrd->response_type) {
			    case Response_Filter:
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
			    }
			    action_num = i;
			}
			format_count++;
		    }
		}
		mrd = stone->actions[action_num].o.imm.mutable_response_data;
		instance = generate_filter_code(mrd, conversion_target_format);
		if (instance == 0) return 0;
		switch(mrd->response_type) {
		case Response_Filter:
		    INT_EVassoc_mutated_imm_action(cm, stone->local_id, action_num, 
					       filter_wrapper, instance, 
					       conversion_target_format);
		case Response_Transform:
		    INT_EVassoc_mutated_imm_action(cm, stone->local_id, action_num, 
					       transform_wrapper, instance, 
					       conversion_target_format);
		}

		return_value = 1;
	    }
	}
	if (event->event_encoded && (conversion_target_format != NULL)) {
	    /* create a decode action */
	    INT_EVassoc_conversion_action(cm, stone->local_id, 
				      conversion_target_format, 
				      event->reference_format);
/*	    printf(" Returning ");
	    dump_action(stone, a, "   ");*/
	    return_value = 1;
	}
    }
    return return_value;
}

void
response_data_free(){}


static void
add_standard_routines(context)
ecl_parse_context context;
{
    static char extern_string[] = "\
		int printf(string format, ...);\n\
		long lrand48();\n\
		double drand48();\n";

    static ecl_extern_entry externs[] = {
	{"printf", (void *) 0},
	{"lrand48", (void *) 0},
	{"drand48", (void *) 0},
	{(void *) 0, (void *) 0}
    };
    /* 
     * some compilers think it isn't a static initialization to put this
     * in the structure above, so do it explicitly.
     */
    externs[0].extern_value = (void *) (long) printf;
    externs[1].extern_value = (void *) (long) lrand48;
    externs[2].extern_value = (void *) (long) drand48;

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

static response_instance
generate_filter_code(mrd, format)
struct response_spec *mrd;
IOFormat format;
{
    response_instance instance = malloc(sizeof(*instance));

    ecl_code code;
    ecl_parse_context parse_context = new_ecl_parse_context();
    /*    sm_ref conn_info_data_type, conn_info_param;*/

    add_standard_routines(parse_context);

    switch (mrd->response_type) {
    case Response_Filter:
    case Response_Transform:
	add_param(parse_context, "input", 0, format);

	if (mrd->response_type == Response_Transform) {
	    add_param(parse_context, "output", 1, 
		      mrd->u.transform.reference_output_format);
	}
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
	code = ecl_code_gen(mrd->u.filter.function, parse_context);
	instance->response_type = Response_Filter;
	instance->u.filter.code = code;
	break;
    case Response_Transform:
	code = ecl_code_gen(mrd->u.transform.function, parse_context);
	instance->response_type = Response_Transform;
	instance->u.transform.code = code;
	instance->u.transform.out_size = 
	    mrd->u.transform.output_base_struct_size;
	instance->u.transform.out_format = 
	    mrd->u.transform.reference_output_format;
	break;
    }
    ecl_free_parse_context(parse_context);

    return instance;
}

