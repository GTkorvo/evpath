#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>

#include "evpath.h"
#include "cm_internal.h"

static IOFormat register_format_set(CManager cm, CMFormatList list, 
				    IOContext *context_ptr);


void
EVPSubmit_encoded(CManager cm, int local_path_id, void *data, int len)
{
    /* build data record for event and enter it into queue for path */
    /* apply actions to data until none remain (no writes) */
    /* do writes, dereferencing data as necessary */
    /* check data record to see if it is still referenced and act accordingly*/
}

void
EVPSubmit_general(CManager cm, int local_path_id, event_item *event)
{
    
}

EVstone
EValloc_stone(CManager cm)
{
    event_path_data evp = cm->evp;
    int stone_num = evp->stone_count++;
    stone_type stone;

    evp->stone_map = realloc(evp->stone_map, 
			     (evp->stone_count * sizeof(evp->stone_map[0])));
    stone = &evp->stone_map[stone_num];
    memset(stone, 0, sizeof(*stone));
    stone->local_id = stone_num;
    stone->default_action = -1;
    return stone_num;
}

void
EVassoc_terminal_action(CManager cm, EVstone stone_num, 
			CMFormatList format_list, EVSimpleHandlerFunc handler,
			void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone = &evp->stone_map[stone_num];
    int proto_action_num = stone->proto_action_count++;
    stone->proto_actions = realloc(stone->proto_actions, 
				   (proto_action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[proto_action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    stone->proto_actions[proto_action_num].input_format_requirements =
	format_list;
    stone->proto_actions[proto_action_num].action_type = Action_Terminal;
    stone->proto_actions[proto_action_num].t.term.handler = handler;
    stone->proto_actions[proto_action_num].t.term.client_data = client_data;
    stone->proto_actions[proto_action_num].reference_format = 
	register_format_set(cm, format_list, NULL);
    action_num = stone->action_count++;
    stone->actions = realloc(stone->actions, (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, sizeof(stone->actions[0]));
    stone->actions[action_num].action_type = Action_Terminal;
    stone->actions[action_num].requires_decoded = 1;
    stone->actions[action_num].reference_format = 
	stone->proto_actions[proto_action_num].reference_format;
    stone->actions[action_num].o.terminal_proto_action_number = proto_action_num;
}
    

static int evpath_locked(){return 1;}

static void
enqueue_event(CManager cm, action *act, event_item *event)
{
/*    event_path_data evp = cm->evp;*/
    queue_item *item = malloc(sizeof(*item));
    item->item = event;
    event->ref_count++;
    if (act->queue_head == NULL) {
	act->queue_head = item;
	act->queue_tail = item;
	item->next = NULL;
    } else {
	act->queue_tail->next = item;
	act->queue_tail = item;
	item->next = NULL;
    }
}

static event_item *
dequeue_event(CManager cm, action *act)
{
/*    event_path_data evp = cm->evp;*/
    queue_item *item = act->queue_head;
    event_item *event = NULL;
    if (item == NULL) return event;
    event = item->item;
    event->ref_count++;
    if (act->queue_head == act->queue_tail) {
	act->queue_head = NULL;
	act->queue_tail = NULL;
    } else {
	act->queue_head = act->queue_head->next;
    }
    free(item);
    return event;
}

static void
set_conversions(IOContext ctx, IOFormat src_format, IOFormat target_format)
{
    IOFormat* subformat_list, *saved_subformat_list, *target_subformat_list;
    subformat_list = get_subformats_IOformat(src_format);
    saved_subformat_list = subformat_list;
    target_subformat_list = get_subformats_IOformat(target_format);
    while((subformat_list != NULL) && (*subformat_list != NULL)) {
	char *subformat_name = name_of_IOformat(*subformat_list);
	int j = 0;
	while(target_subformat_list && 
	      (target_subformat_list[j] != NULL)) {
	    if (strcmp(name_of_IOformat(target_subformat_list[j]), 
		       subformat_name) == 0) {
		IOFieldList sub_field_list;
		int sub_struct_size;
		sub_field_list = 
		    field_list_of_IOformat(target_subformat_list[j]);
		sub_struct_size = struct_size_field_list(sub_field_list, 
							 sizeof(char*));
		set_conversion_IOcontext(ctx, *subformat_list,
					 sub_field_list,
					 sub_struct_size);
	    }
	    j++;
	}
	subformat_list++;
    }
    free(saved_subformat_list);
}

static action *
determine_action(CManager cm, stone_type stone, event_item *event)
{
    event_path_data evp = cm->evp;
    int i;
    int nearest_proto_action = -1;
    for (i=0; i < stone->action_count; i++) {
	if (stone->actions[i].reference_format == event->reference_format) {
	    if ((event->contents == Event_Encoded_CM_Owned) &&
		stone->actions[i].requires_decoded) {
		continue;
	    }
	    return &stone->actions[i];
	}
    }
    if (stone->proto_action_count > 0) {
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

    if (nearest_proto_action == -1) {
	if (stone->default_action != -1) {
	    return &stone->actions[stone->default_action];
	}
	return NULL;
    }
    /* This format is to be bound with action nearest_proto_action */
    if (1) {
	if (event->contents == Event_Encoded_CM_Owned) {
	    /* create a decode action */
	    int a = stone->action_count++;
	    int id_len;
	    IOFormat format;
	    char *server_id = get_server_ID_IOformat(event->reference_format,
						     &id_len);
	    action *act;
	    stone->actions = realloc(stone->actions, 
				     sizeof(stone->actions[0]) * (a + 1));
	    act = & stone->actions[a];
	    act->action_type = Action_Decode;
	    act->reference_format = event->reference_format;
	    act->queue_head = act->queue_tail = NULL;


	    act->o.decode.context = create_IOsubcontext(evp->root_context);
	    format = get_format_app_IOcontext(act->o.decode.context, 
					      server_id, NULL);
	    act->o.decode.decode_format = format;
	    act->o.decode.target_reference_format = 
		stone->proto_actions[nearest_proto_action].reference_format;
	    set_conversions(act->o.decode.context, format,
			    act->o.decode.target_reference_format);
	    return act;
	}
    }
}

static event_item *
decode_action(CManager cm, event_item *event, action *act)
{
    if (event->contents == Event_Encoded_CM_Owned) {
	if (decode_in_place_possible(act->o.decode.decode_format)) {
	    void *decode_buffer;
	    if (!decode_in_place_IOcontext(act->o.decode.context,
					   event->encoded_event, 
					   (void**) (long) &decode_buffer)) {
		printf("Decode failed\n");
		return 0;
	    }
	    event->decoded_event = decode_buffer;
	    event->contents = Event_Unencoded_CM_Owned;
	    return event;
	} else {
	    int decoded_length = this_IOrecord_length(act->o.decode.context, 
						      event->encoded_event, 
						      event->event_len);
	    CMbuffer cm_decode_buf = cm_get_data_buf(cm, decoded_length);
	    void *decode_buffer = cm_decode_buf->buffer;
	    decode_to_buffer_IOcontext(act->o.decode.context, 
				       event->encoded_event, decode_buffer);
/*	    cm_return_data_buf(conn->partial_buffer);*/
	    event->decoded_event = decode_buffer;
	    event->contents = Event_Unencoded_CM_Owned;
	    event->reference_format = act->o.decode.target_reference_format;
	    return event;
	}
    } else {
	assert(0);
    }
}

static
int
internal_path_submit(CManager cm, int local_path_id, event_item *event)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    action *act = NULL;

    assert(evpath_locked());
    if (evp->stone_count < local_path_id) {
	return -1;
    }
    stone = &(evp->stone_map[local_path_id]);
    act = determine_action(cm, stone, event);
    if (act->action_type == Action_Decode) {
	CMtrace_out(cm, EVerbose, "Decoding event");
	event = decode_action(cm, event, act);
	act = determine_action(cm, stone, event);
    }
    CMtrace_out(cm, EVerbose, "Enqueueing event");
    enqueue_event(cm, act, event);
    return 1;
}

static
int
process_local_actions(cm)
CManager cm;
{
    event_path_data evp = cm->evp;
    int s, a;
    CMtrace_out(cm, EVerbose, "Process local actions");
    for (s = 0; s < evp->stone_count; s++) {
	for (a=0 ; a < evp->stone_map[s].action_count; a++) {
	    action *act = &evp->stone_map[s].actions[a];
	    if ((act->action_type == Action_Terminal) && 
		(act->queue_head != NULL)) {
		/* the data should already be in the right format */
		int proto = act->o.terminal_proto_action_number;
		struct terminal_proto_vals *term = 
		    &evp->stone_map[s].proto_actions[proto].t.term;
		EVSimpleHandlerFunc handler = term->handler;
		void *client_data = term->client_data;
		event_item *event = dequeue_event(cm, act);
		CMtrace_out(cm, EVerbose, "Executing terminal event");
		(handler)(cm, event->decoded_event, client_data, NULL);
		CMtrace_out(cm, EVerbose, "Finish terminal event");
	    }
	}
    }
}

static
int
process_output_actions(CManager cm)
{
    event_path_data evp = cm->evp;
    int s, a;
    CMtrace_out(cm, EVerbose, "Process output actions");
    for (s = 0; s < evp->stone_count; s++) {
	for (a=0 ; a < evp->stone_map[s].action_count; a++) {
	    action *act = &evp->stone_map[s].actions[a];
	    if ((act->action_type == Action_Output) && 
		(act->queue_head != NULL)) {
		CMtrace_out(cm, EVerbose, "Writing event");
		internal_write_event(act->o.out.conn, 
				     act->queue_head->item->format,
				     &act->o.out.remote_stone_id, 4, 
				     act->queue_head->item, NULL);
	    }
	}
    }	    
    return 1;
}

extern void
EVassoc_output_action(CManager cm, EVstone stone_num, CMFormatList format_list, 
		      attr_list contact_list, EVstone remote_stone)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    int action_num = stone->action_count++;
    stone->actions = realloc(stone->actions, 
				   (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, 
	   sizeof(stone->actions[0]));
    stone->actions[action_num].action_type = Action_Output;
    stone->actions[action_num].o.out.conn = CMget_conn(cm, contact_list);
    stone->actions[action_num].o.out.remote_stone_id = remote_stone;
    stone->default_action = action_num;
}

static int
register_subformats(context, field_list, sub_list)
IOContext context;
IOFieldList field_list;
CMFormatList sub_list;
{
    char **subformats = get_subformat_names(field_list);
    char **save_subformats = subformats;

    if (subformats != NULL) {
	while (*subformats != NULL) {
	    int i = 0;
	    if (get_IOformat_by_name_IOcontext(context, *subformats) != NULL) {
		/* already registered this subformat */
		goto next_format;
	    }
	    while (sub_list && (sub_list[i].format_name != NULL)) {
		if (strcmp(sub_list[i].format_name, *subformats) == 0) {
		    IOFormat tmp;
		    if (register_subformats(context, sub_list[i].field_list,
					      sub_list) != 1) {
			fprintf(stderr, "Format registration failed for subformat \"%s\"\n",
				sub_list[i].format_name);
			return 0;
		    }
		    tmp = register_IOcontext_format(*subformats,
						    sub_list[i].field_list,
						    context);
		    if (tmp == NULL) {
			fprintf(stderr, "Format registration failed for subformat \"%s\"\n",
				sub_list[i].format_name);
			return 0;
		    }
		    goto next_format;
		}
		i++;
	    }
	    fprintf(stderr, "Subformat \"%s\" not found in format list\n",
		    *subformats);
	    return 0;
	next_format:
	    free(*subformats);
	    subformats++;
	}
    }
    free(save_subformats);
    return 1;
}

static IOFormat
register_format_set(CManager cm, CMFormatList list, IOContext *context_ptr)
{
    event_path_data evp = cm->evp;
    char *server_id;
    int id_len;
    IOFormat format;
    IOContext tmp_context = create_IOsubcontext(evp->root_context);

    if (register_subformats(tmp_context, list[0].field_list, list) != 1) {
	fprintf(stderr, "Format registration failed for format \"%s\"\n",
		list[0].format_name);
	return 0;
    }
    format = register_opt_format(list[0].format_name, 
				 list[0].field_list, NULL, 
				 tmp_context);
    server_id = get_server_ID_IOformat(format, &id_len);

    if (context_ptr == NULL) {
	/* replace original ref with ref in base context */
	format = get_format_app_IOcontext(evp->root_context, server_id, NULL);

	free_IOsubcontext(tmp_context);
    } else {
	*context_ptr = tmp_context;
    }
    return format;
}

extern EVsource
EVcreate_submit_handle(CManager cm, EVstone stone, CMFormatList data_format)
{
    EVsource source = malloc(sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    source->format = CMregister_format(cm, data_format[0].format_name,
				       data_format[0].field_list, data_format);
    CMcomplete_format_registration(source->format, 0);
    source->reference_format = source->format->format;
    return source;
}

extern void
internal_cm_network_submit(CManager cm, CMbuffer cm_data_buf, 
			   CMConnection conn, void *buffer, int stone_id)
{
    event_path_data evp = cm->evp;
    event_item *event = malloc(sizeof(event_item));
    memset(event, 0, sizeof(event_item));
    event->ref_count = 1;
    event->contents = Event_Encoded_CM_Owned;
    event->encoded_event = buffer;
    event->reference_format = get_format_app_IOcontext(evp->root_context, 
					     buffer, conn);
    event->format = NULL;
    internal_path_submit(cm, stone_id, event);
    process_local_actions(cm);
    process_output_actions(cm);
}

void
EVsubmit(EVsource source, void *data, attr_list attrs)
{
    event_item *event = malloc(sizeof(event_item));
    memset(event, 0, sizeof(event_item));
    event->ref_count = 1;
    event->contents = Event_Unencoded_App_Owned;
    event->decoded_event = data;
    event->reference_format = source->reference_format;
    event->format = source->format;
    internal_path_submit(source->cm, source->local_stone_id, event);
    process_local_actions(source->cm);
    process_output_actions(source->cm);
}

void
EVPinit(CManager cm)
{
    cm->evp = malloc(sizeof( struct _event_path_data));
    memset(cm->evp, 0, sizeof( struct _event_path_data));
    cm->evp->root_context = create_IOcontext();
}
