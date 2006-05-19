
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>

#include "evpath.h"
#include "cm_internal.h"
#include "response.h"

extern 
IOFormat EVregister_format_set(CManager cm, CMFormatList list, 
				    IOContext *context_ptr);
static void reference_event(event_item *event);
static void dump_action(stone_type stone, int a, const char *indent);
extern void print_server_ID(char *server_id);
static void dump_stone(stone_type stone);

static const char *action_str[] = { "Action_Output", "Action_Terminal", "Action_Filter", "Action_Immediate", "Action_Decode", "Action_Split", "Action_Select"};

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
INT_EValloc_stone(CManager cm)
{
    event_path_data evp = cm->evp;
    int stone_num = evp->stone_count;
    stone_type stone;

    evp->stone_map = realloc(evp->stone_map, 
			     (evp->stone_count + 1) * sizeof(evp->stone_map[0]));
    stone = &evp->stone_map[stone_num];
    memset(stone, 0, sizeof(*stone));
    stone->local_id = stone_num;
    stone->default_action = -1;
    stone->response_cache_count = 0;
    stone->response_cache = NULL;
    stone->is_frozen = 0;
    stone->is_processing = 0;
    stone->is_outputting = 0;
    stone->is_draining = 0;
    stone->queue = malloc(sizeof(queue_struct));
    stone->queue->queue_tail = stone->queue->queue_head = NULL;
    stone->proto_actions = NULL;
    stone->stone_attrs = CMcreate_attr_list(cm);
    evp->stone_count++;
    return stone_num;
}

void
INT_EVfree_stone(CManager cm, EVstone stone_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int i;

    stone = &evp->stone_map[stone_num];
    if (stone->local_id == -1) return;
    if (stone->periodic_handle != NULL) {
	INT_CMremove_task(stone->periodic_handle);
	stone->periodic_handle = NULL;
    }
    for(i = 0; i < stone->proto_action_count; i++) {
	proto_action *act = &stone->proto_actions[i];
	if (act->attrs != NULL) {
	    INT_CMfree_attr_list(cm, act->attrs);
	}
	switch(act->action_type) {
	case Action_NoAction:
	    break;
	case Action_Output:
	    if (act->o.out.remote_path) 
		free(act->o.out.remote_path);
	    break;
	case Action_Terminal:
	    break;
	case Action_Filter:
	    break;
	case Action_Decode:
	    if (act->o.decode.context) {
		free_IOcontext(act->o.decode.context);
		act->o.decode.context = NULL;
	    }
	    break;
	case Action_Split:
	    free(act->o.split_stone_targets);
	    break;
	case Action_Immediate:
	    if (act->o.imm.mutable_response_data != NULL) {
		response_data_free(cm, act->o.imm.mutable_response_data);
	    }
	    free(act->o.imm.output_stone_ids);
	    break;
	}
    }
    for(i = 0; i < stone->response_cache_count; i++) {
	response_cache_element *resp = &stone->response_cache[i];
	switch(resp->action_type) {
	case Action_Decode:
	    if (resp->o.decode.context) {
		free_IOcontext(resp->o.decode.context);
		resp->o.decode.context = NULL;
	    }
	    break;
	case Action_Immediate:
	    break;
	default:
	    break;
	}
    }
    free(stone->queue);
    if (stone->response_cache) free(stone->response_cache);
    free(stone->proto_actions);
    stone->queue = NULL;
    stone->local_id = -1;
    stone->proto_action_count = 0;
    stone->proto_actions = NULL;
    if (stone->stone_attrs != NULL) {
	INT_CMfree_attr_list(cm, stone->stone_attrs);
	stone->stone_attrs = NULL;
    }
}

static void
clear_response_cache(stone_type stone)
{
    stone->response_cache_count = 0;
    if (stone->response_cache) free(stone->response_cache);
    stone->response_cache = NULL;
}

EVstone
INT_EVcreate_terminal_action(CManager cm, 
			CMFormatList format_list, EVSimpleHandlerFunc handler,
			void *client_data)
{
    EVstone stone = INT_EValloc_stone(cm);
    (void) INT_EVassoc_terminal_action(cm, stone, format_list, 
				       handler, client_data);
    return stone;
}    

EVaction
INT_EVassoc_terminal_action(CManager cm, EVstone stone_num, 
			CMFormatList format_list, EVSimpleHandlerFunc handler,
			void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone = &evp->stone_map[stone_num];
    int proto_action_num = stone->proto_action_count;
    stone->proto_actions = realloc(stone->proto_actions, 
				   (proto_action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[proto_action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    stone->proto_actions[proto_action_num].input_format_requirements =
	format_list;
    stone->proto_actions[proto_action_num].action_type = Action_Terminal;
    stone->proto_actions[proto_action_num].o.term.handler = handler;
    stone->proto_actions[proto_action_num].o.term.client_data = client_data;
    stone->proto_actions[proto_action_num].reference_format = NULL;
    stone->proto_actions[proto_action_num].queue = stone->queue;
    stone->proto_actions[proto_action_num].requires_decoded = 1;
    if (format_list != NULL) {
	stone->proto_actions[proto_action_num].reference_format = 
	    EVregister_format_set(cm, format_list, NULL);
    }	
    action_num = stone->response_cache_count;
    clear_response_cache(stone);
    stone->response_cache = realloc(stone->response_cache, (action_num + 1) * 
				   sizeof(stone->response_cache[0]));
    memset(&stone->response_cache[action_num], 0, sizeof(stone->response_cache[0]));
    stone->response_cache[action_num].action_type = Action_Terminal;
    stone->response_cache[action_num].requires_decoded = 1;
    stone->response_cache[action_num].reference_format = 
	stone->proto_actions[proto_action_num].reference_format;
    stone->response_cache[action_num].proto_action_id = proto_action_num;
    stone->proto_action_count++;
    if (CMtrace_on(cm, EVerbose)) {
	printf("Adding Terminal action %d to stone %d\n", action_num, 
	       stone_num);
	printf("Stone dump->\n");
	dump_stone(stone);
    }
    return proto_action_num;
}
    

EVstone
INT_EVcreate_immediate_action(CManager cm, char *action_spec, 
			      EVstone *target_list)
{
    int i = 0;
    EVstone stone = INT_EValloc_stone(cm);
    EVaction action = EVassoc_immediate_action(cm, stone, action_spec, NULL);
    while (target_list && (target_list[i] != 0)) {
	INT_EVaction_set_output(cm, stone, action, i, target_list[i]);
    }
    return stone;
}

EVaction
INT_EVassoc_immediate_action(CManager cm, EVstone stone_num, 
			 char *action_spec, void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone = &evp->stone_map[stone_num];

    action_num = stone->proto_action_count;
    CMtrace_out(cm, EVerbose, "Adding Immediate action %d to stone %d",
		action_num, stone_num);
    stone->proto_actions = realloc(stone->proto_actions, (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].requires_decoded = 1;
    stone->proto_actions[action_num].queue = stone->queue;
    stone->proto_actions[action_num].action_type = Action_Immediate;
    stone->proto_actions[action_num].o.imm.output_count = 0;
    stone->proto_actions[action_num].o.imm.output_stone_ids = malloc(sizeof(int));
    stone->proto_actions[action_num].o.imm.output_stone_ids[0] = -1;
    stone->proto_actions[action_num].o.imm.mutable_response_data = 
 	install_response_handler(cm, stone_num, action_spec, client_data, 
				 &stone->proto_actions[action_num].reference_format);
   stone->proto_action_count++;
    clear_response_cache(stone);
    return action_num;
}

EVstone
INT_EVassoc_filter_action(CManager cm, EVstone stone_num, 
		      CMFormatList format_list, EVSimpleHandlerFunc handler,
		      EVstone out_stone_num, void *client_data)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    int proto_action_num = stone->proto_action_count;
    stone->proto_actions = realloc(stone->proto_actions, 
				   (proto_action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[proto_action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    stone->proto_actions[proto_action_num].input_format_requirements =
	format_list;
    stone->proto_actions[proto_action_num].action_type = Action_Filter;
    stone->proto_actions[proto_action_num].o.term.handler = handler;
    stone->proto_actions[proto_action_num].o.term.client_data = client_data;
    stone->proto_actions[proto_action_num].o.term.target_stone_id = out_stone_num;
    stone->proto_actions[proto_action_num].requires_decoded = 1;
    stone->proto_actions[proto_action_num].reference_format = NULL;
    stone->proto_actions[proto_action_num].queue = stone->queue;
    if (format_list != NULL) {
	stone->proto_actions[proto_action_num].reference_format = 
	    EVregister_format_set(cm, format_list, NULL);
    }	
    stone->proto_action_count++;
    clear_response_cache(stone);
    return proto_action_num;
}
    

static int evpath_locked(){return 1;}

static void
enqueue_event(CManager cm, int stone_id, int action_id, event_item *event)
{
    event_path_data evp = cm->evp;
    queue_item *item;
    stone_type stone = &evp->stone_map[stone_id];
    response_cache_element *resp = &stone->response_cache[action_id];
    int proto_id = resp->proto_action_id;
    proto_action *act = &stone->proto_actions[proto_id];
    if (evp->queue_items_free_list == NULL) {
	item = malloc(sizeof(*item));
    } else {
	item = evp->queue_items_free_list;
	evp->queue_items_free_list = item->next;
    }
    item->item = event;
    item->action_id = action_id;
    reference_event(event);
    if (act->queue->queue_head == NULL) {
	act->queue->queue_head = item;
	act->queue->queue_tail = item;
	item->next = NULL;
    } else {
	act->queue->queue_tail->next = item;
	act->queue->queue_tail = item;
	item->next = NULL;
    }
}

static event_item *
dequeue_event(CManager cm, queue_ptr q, int *act_p)
{
    event_path_data evp = cm->evp;
    queue_item *item = q->queue_head;
    event_item *event = NULL;
    if (item == NULL) return event;
    event = item->item;
    *act_p = item->action_id;
    if (q->queue_head == q->queue_tail) {
	q->queue_head = NULL;
	q->queue_tail = NULL;
    } else {
	q->queue_head = q->queue_head->next;
    }
    item->next = evp->queue_items_free_list;
    evp->queue_items_free_list = item;
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
	int j = 0, found = 0;
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
		found++;
	    }
	    j++;
	}
	if (!found && (subformat_list[1] != NULL)) {
	    printf("No match for %s\n", 
		   global_name_of_IOformat(*subformat_list));
	}
	subformat_list++;
    }
    if (!has_conversion_IOformat(src_format)) {
	IOFieldList sub_field_list;
	int sub_struct_size;
	sub_field_list = field_list_of_IOformat(target_format);
	sub_struct_size = struct_size_field_list(sub_field_list, 
						 sizeof(char*));
	set_conversion_IOcontext(ctx, src_format, sub_field_list,
				 sub_struct_size);
    }
    free(saved_subformat_list);
    free(target_subformat_list);
}

extern void
INT_EVassoc_conversion_action(cm, stone_id, target_format, incoming_format)
CManager cm;
int stone_id;
IOFormat target_format;
IOFormat incoming_format;
{
    response_cache_element *act;
    stone_type stone = &(cm->evp->stone_map[stone_id]);
    int a = stone->response_cache_count;
    int id_len;
    IOFormat format;

    char *server_id = get_server_ID_IOformat(incoming_format,
						     &id_len);
    CMtrace_out(cm, EVerbose, "Adding Conversion action %d to stone %d",
		a, stone_id);
    if (CMtrace_on(cm, EVerbose)) {
	char *target_tmp = global_name_of_IOformat(target_format);
	char *incoming_tmp = global_name_of_IOformat(incoming_format);
	printf("   Incoming format is %s, target %s\n", incoming_tmp, 
	       target_tmp);
    }
    stone->response_cache = realloc(stone->response_cache,
			     sizeof(stone->response_cache[0]) * (a + 1));
    act = & stone->response_cache[a];
    memset(act, 0, sizeof(*act));
    act->requires_decoded = 0;
    act->action_type = Action_Decode;
    act->reference_format = incoming_format;

    act->o.decode.context = create_IOsubcontext(cm->evp->root_context);
    format = get_format_app_IOcontext(act->o.decode.context, 
				      server_id, NULL);
    act->o.decode.decode_format = format;
    act->o.decode.target_reference_format = target_format;
    set_conversions(act->o.decode.context, format,
		    act->o.decode.target_reference_format);
    stone->response_cache_count++;
}

int
INT_EVaction_set_output(CManager cm, EVstone stone_num, EVaction act_num, 
		    int output_index, EVstone output_stone)
{
    stone_type stone;
    int output_count = 0;
    if (stone_num > cm->evp->stone_count) return 0;
    stone = &(cm->evp->stone_map[stone_num]);
    if (act_num > stone->proto_action_count) return 0;
    assert(stone->proto_actions[act_num].action_type == Action_Immediate);
    CMtrace_out(cm, EVerbose, "Setting output %d on stone %d to local stone %d",
		output_index, stone_num, output_stone);
    output_count = stone->proto_actions[act_num].o.imm.output_count;
    if (output_index >= output_count) {
	stone->proto_actions[act_num].o.imm.output_stone_ids = 
	    realloc(stone->proto_actions[act_num].o.imm.output_stone_ids,
		    sizeof(int) * (output_index + 2));
	for ( ; output_count < output_index; output_count++) {
	    stone->proto_actions[act_num].o.imm.output_stone_ids[output_count] = -1;
	}
	stone->proto_actions[act_num].o.imm.output_count = output_index + 1;
    }
    stone->proto_actions[act_num].o.imm.output_stone_ids[output_index] = output_stone;
    return 1;
}
    

static int
determine_action(CManager cm, stone_type stone, event_item *event)
{
    int i;
    int return_response;
    if (event->reference_format == NULL) {
	CMtrace_out(cm, EVerbose, "Call to determine_action, event reference_format is NULL");
    } else {
	CMtrace_out(cm, EVerbose, "Call to determine_action, event reference_format is %lx (%s)",
	   event->reference_format, name_of_IOformat(event->reference_format));
    }
    for (i=0; i < stone->response_cache_count; i++) {
	if (stone->response_cache[i].reference_format == event->reference_format) {
	    /* 
	     * if the event is encoded and the action requires decoded data,
	     * this action won't do.  Scan further for decode action or 
	     * generate one with response_determination().
	     */
	    if (event->event_encoded && stone->response_cache[i].requires_decoded) {
		continue;
	    }
	    if (!event->event_encoded && 
		(stone->response_cache[i].action_type == Action_Decode)) {
		continue;
	    }
	    /*
	     * If the incoming event has no specific format (auto event or 
	     * other) and we didn't match something more specific above, 
	     * fall through to response_determination. 
	     */
	    if (event->reference_format == NULL) {
		continue;
	    }
	    return i;
	}
    }
    if (response_determination(cm, stone, event) == 1) {
	return determine_action(cm, stone, event);
    }
    /* 
     * there was no action for this event, install a dummy so we 
     * don't search again.
     */
    if (stone->response_cache_count == 0) {
	stone->response_cache = malloc(sizeof(stone->response_cache[0]));
    } else {
	stone->response_cache = 
	    realloc(stone->response_cache,
		    (stone->response_cache_count + 1) * sizeof(stone->response_cache[0]));
    }
    stone->response_cache[stone->response_cache_count].reference_format =
	event->reference_format;
    return_response = stone->response_cache_count++;

    if (stone->default_action != -1) {
/*	    printf(" Returning ");
	    dump_action(stone, stone->default_action, "   ");*/
	response_cache_element *resp = 
	    &stone->response_cache[return_response];
	proto_action *proto = &stone->proto_actions[stone->default_action];
	resp->proto_action_id = stone->default_action;
	resp->action_type = proto->action_type;
	resp->requires_decoded = proto->requires_decoded;
	return return_response;
    }
    if (CMtrace_on(cm, EVWarning)) {
	printf("Warning!  No action found for incoming event on stone %d\n",
	       stone->local_id);
	dump_stone(stone);
    }
    stone->response_cache[return_response].action_type = Action_NoAction;

    return return_response;
}

/*   GSE
 *   Need to seriously augment buffer handling.  In particular, we have to
 *   formalize the handling of event buffers.  Make sure we have all the
 *   situations covered, try to keep both encoded and decoded versions of
 *   events where possible.  Try to augment testing because many cases are
 *   not covered in homogeneous regression testing (our normal mode).

 *   Possible event situations:
 *  Event_CM_Owned:   This is an event that came in from the network, so CM 
 *     owns the buffer that it lives in.   The event maybe encoded or decoded, 
 *     but whatever the situation, we need to do INT_CMreturn_buffer() to 
 *     free it.   ioBuffer should be NULL;  The 'cm' value tells us where to 
 *     do cmreturn_buffer() .
 *  Event_Freeable:   This is an event that came from a higher-level and 
 *     is provided with a free() routine that we should call when we are 
 *     finished with it.  We are free to return control the the application 
 *     while still retaining the event as long as we later call the free 
 *     routine. 
 *  Event_App_Owned:  This is an event that came from a higher-level and 
 *     is *NOT* provided with a free() routine.  We should not return 
 *     until we are done processing it..
 */

event_item *
get_free_event(event_path_data evp)
{
    event_item *event = malloc(sizeof(*event));
    memset(event, 0, sizeof(event_item));
    event->ref_count = 1;
    event->event_len = -1;
    event->ioBuffer = NULL;
    return event;
}

extern void
return_event(event_path_data evp, event_item *event)
{
    event->ref_count--;
    if (event->ref_count == 0) {
	/* return event memory */
	switch (event->contents) {
	case Event_CM_Owned:
	    if (event->decoded_event) {
		INT_CMreturn_buffer(event->cm, event->decoded_event);
	    } else {
		INT_CMreturn_buffer(event->cm, event->encoded_event);
	    }
	    break;
	case Event_Freeable:
	    (event->free_func)(event->decoded_event, event->free_arg);
	    break;
	case Event_App_Owned:
	    if (event->free_func) {
		(event->free_func)(event->free_arg, NULL);
	    }
	    break;
	}
	if (event->attrs != NULL) INT_CMfree_attr_list(event->cm, event->attrs);
	if (event->ioBuffer != NULL)
	free(event);
    }
}

static event_item *
decode_action(CManager cm, event_item *event, response_cache_element *act)
{
    event_path_data evp = cm->evp;
    if (!event->event_encoded) {
	assert(0);
    }
	
    switch(event->contents) {
    case Event_CM_Owned:
	if (decode_in_place_possible(act->o.decode.decode_format)) {
	    void *decode_buffer;
	    if (!decode_in_place_IOcontext(act->o.decode.context,
					   event->encoded_event, 
					   (void**) (long) &decode_buffer)) {
		printf("Decode failed\n");
		return 0;
	    }
	    event->decoded_event = decode_buffer;
	    event->encoded_event = NULL;
	    event->event_encoded = 0;
	    event->reference_format = act->o.decode.target_reference_format;
	    return event;
	} else {
	    int decoded_length = this_IOrecord_length(act->o.decode.context, 
						      event->encoded_event,
						      event->event_len);
	    CMbuffer cm_decode_buf = cm_get_data_buf(cm, decoded_length);
	    void *decode_buffer = cm_decode_buf->buffer;
	    if (event->event_len == -1) printf("BAD LENGTH\n");
	    decode_to_buffer_IOcontext(act->o.decode.context, 
				       event->encoded_event, decode_buffer);
	    INT_CMtake_buffer(cm, decode_buffer);
	    event->decoded_event = decode_buffer;
	    event->event_encoded = 0;
	    event->reference_format = act->o.decode.target_reference_format;
	    return event;
	}
	break;
    case Event_Freeable:
    case Event_App_Owned:
    {
	/* can't do anything with the old event, make a new one */
	int decoded_length = this_IOrecord_length(act->o.decode.context, 
						  event->encoded_event,
						  event->event_len);
	event_item *new_event = get_free_event(evp);
	CMbuffer cm_decode_buf = cm_get_data_buf(cm, decoded_length);
	void *decode_buffer = cm_decode_buf->buffer;
	if (event->event_len == -1) printf("BAD LENGTH\n");
	decode_to_buffer_IOcontext(act->o.decode.context, 
				   event->encoded_event, decode_buffer);
	INT_CMtake_buffer(cm, decode_buffer);
	new_event->decoded_event = decode_buffer;
	new_event->event_encoded = 0;
	new_event->encoded_event = NULL;
	new_event->event_len = 0;
	new_event->encoded_eventv = NULL;
	new_event->cm = cm;
	new_event->reference_format = act->o.decode.target_reference_format;
	new_event->contents = Event_CM_Owned;
	if (event->attrs) {
	    new_event->attrs = attr_copy_list(event->attrs);
	} else {
	    new_event->attrs = NULL;
	}
	return new_event;
	break;
    }
    }
    return NULL;   /* shouldn't ever happen */
}

static void
encode_event(CManager cm, event_item *event)
{
    event_path_data evp = cm->evp;
    if (event->event_encoded) {
	assert(0);
    }
	
    if (event->ioBuffer != NULL) return;  /* already encoded */
    event->ioBuffer = create_IOBuffer();
    event->encoded_event = 
	encode_IOcontext_bufferB(evp->root_context, event->reference_format,
				 event->ioBuffer, event->decoded_event,
				 &event->event_len);
}

extern void
ecl_encode_event(CManager cm, event_item *event)
{
    encode_event(cm, event);
}

static void
dump_proto_action(stone_type stone, int a, const char *indent)
{
    proto_action *proto = &stone->proto_actions[a];
    printf(" Proto-Action %d - %s\n", a, action_str[proto->action_type]);
}

static void
dump_action(stone_type stone, int a, const char *indent)
{
    proto_action *act = &stone->proto_actions[a];
    printf(" Action %d - %s  ", a, action_str[act->action_type]);
    if (act->requires_decoded) {
	printf("requires decoded\n");
    } else {
	printf("accepts encoded\n");
    }
    printf("  expects format ");
    if (act->reference_format) {
	char *tmp;
	printf("\"%s\" \n", tmp = global_name_of_IOformat(act->reference_format));
	free(tmp);
    } else {
	printf(" NULL\n");
    }
    switch(act->action_type) {
    case Action_Output:
	printf("  Target: %s: connection %lx, remote_stone_id %d\n",
	       (act->o.out.remote_path ? act->o.out.remote_path : "NULL" ),
	       (long)(void*)act->o.out.conn, act->o.out.remote_stone_id);
	if (act->o.out.conn != NULL) dump_attr_list(act->o.out.conn->attrs);
	if (act->o.out.conn_failed) printf("Connection has FAILED!\n");
	break;
    case Action_Terminal:
/*	printf("  Terminal proto action number %d\n",
	act->proto_action_id);*/
	break;
    case Action_Filter:
/*	printf("  Filter proto action number %d\n",
	act->proto_actoin_id);*/
	break;
    case Action_Decode:
	printf("   Decoding action\n");
	break;
    case Action_Split:
	printf("    Split target stones: ");
	{
	    int i = 0;
	    while (act->o.split_stone_targets[i] != -1) {
		printf(" %d", act->o.split_stone_targets[i]); i++;
	    }
	    printf("\n");
	}
	break;
    case Action_Immediate: 
	printf("   Immediate action\n");
	dump_mrd(act->o.imm.mutable_response_data);
	break;
    case Action_NoAction:
	printf("   NoAction\n");
	break;
    }
}

static void
dump_stone(stone_type stone)
{
    int i;
    printf("Dump stone ID %d, local addr %lx, default action %d\n",
	   stone->local_id, (long)stone, stone->default_action);
    printf("  proto_action_count %d:\n", stone->proto_action_count);
    for (i=0; i< stone->proto_action_count; i++) {
	dump_proto_action(stone, i, "    ");
    }
    printf("  proto_action_count %d:\n", stone->proto_action_count);
    for (i=0; i< stone->proto_action_count; i++) {
	dump_action(stone, i, "    ");
    }
}

void
EVdump_stone(CManager cm,  EVstone stone_num)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    dump_stone(stone);
}

int
internal_path_submit(CManager cm, int local_path_id, event_item *event)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int resp_id;
    response_cache_element *resp;
    event_item *event_to_submit = event;

    assert(evpath_locked());
    assert(CManager_locked(cm));
    if (evp->stone_count < local_path_id) {
	return -1;
    }
    stone = &(evp->stone_map[local_path_id]);
    resp_id = determine_action(cm, stone, event);
    if (stone->response_cache[resp_id].action_type == Action_NoAction) {
	char *tmp = NULL;
	if (event->reference_format)
	    tmp = global_name_of_IOformat(event->reference_format);
	printf("No action found for event %lx submitted to stone %d\n",
	       (long)event, local_path_id);
	if (tmp != NULL) {
	    static int first = 1;
	    printf("    Unhandled incoming event format was \"%s\"\n", tmp);
	    if (first) {
		first = 0;
		printf("\n\t** use \"format_info <format_name>\" to get full format information ** \n\n");
	    }
	} else {
	    printf("    Unhandled incoming event format was NULL\n");
	}
	free(tmp);
	return 0;
    }
    resp = &stone->response_cache[resp_id];
    if (resp->action_type == Action_Decode) {
	CMtrace_out(cm, EVerbose, "Decoding event, action id %d", resp_id);
	event_to_submit = decode_action(cm, event, resp);
	resp_id = determine_action(cm, stone, event_to_submit);
	resp = &stone->response_cache[resp_id];
    }
    if (CMtrace_on(cm, EVerbose)) {
	printf("Enqueueing event %lx on stone %d, action %lx\n",
	       (long)event_to_submit, local_path_id, (long)resp);
	
	dump_action(stone, resp->proto_action_id, "    ");
    }
    enqueue_event(cm, local_path_id, resp_id, event_to_submit);
    if (event != event_to_submit) {
	/* 
	 * if an event was created by the decode, decrement it's
	 * ref count now.  The parameter 'event' will be dereferenced
	 * by our caller.
	 */
	return_event(evp, event_to_submit);
    }
    return 1;
}

static void
update_event_length_sum(cm, act, event)
CManager cm;
proto_action *act; 
event_item *event;
{
    int eventlength;
    int totallength; 

    /*update act->event_length_sum:*/
    if (get_int_attr(event->attrs, CM_EVENT_SIZE, & eventlength)) {
	if (eventlength >= 0 )
	    act->event_length_sum += eventlength; 
	else 
	    act->event_length_sum = -1; /*received event with undecided size, invalidate length_sum*/
    } else {
	/*attr CM_EVENT_SIZE doesn't exist. 
	  two possibilities: 1. something broken. 2. event is 
	  from application (via EVSubmit, so doesn't have a valid 
	  CM_EVENT_SIZE attr (This is not implemented unless it is 
	  really required by someone.)
	*/
	return;
    } 
    /* also update the EV_EVENT_LSUM attrs */
    if(act->attrs == NULL){
	act->attrs = CMcreate_attr_list(cm);
    }
    totallength = act->event_length_sum;/*1024*/ 
    set_int_attr(act->attrs, EV_EVENT_LSUM, totallength);
}

static
int
process_local_actions(cm)
CManager cm;
{
    event_path_data evp = cm->evp;
    int s, a, more_pending = 0;
    stone_type stone = NULL;
    CMtrace_out(cm, EVerbose, "Process local actions");
    for (s = 0; s < evp->stone_count; s++) {
	if (evp->stone_map[s].local_id == -1) continue;
	if (evp->stone_map[s].is_draining == 1) continue;
	evp->stone_map[s].is_processing = 1;
	while (evp->stone_map[s].queue->queue_head != NULL && evp->stone_map[s].is_draining == 0) {
	    int action_id;
	    event_item *event = dequeue_event(cm, evp->stone_map[s].queue, 
					      &action_id);
	    response_cache_element *act = &evp->stone_map[s].response_cache[action_id];
	    stone = &(evp->stone_map[s]);

	    switch(act->action_type) {
	    case Action_Terminal:
	    case Action_Filter: {
		/* the data should already be in the right format */
		int proto = act->proto_action_id;
		proto_action *p = &stone->proto_actions[proto];
		int out;
		struct terminal_proto_vals *term = &(p->o.term);
		EVSimpleHandlerFunc handler = term->handler;
		void *client_data = term->client_data;
		CMtrace_out(cm, EVerbose, "Executing terminal/filter event");
		update_event_length_sum(cm, p, event);
		cm->evp->current_event_item = event;
		CManager_unlock(cm);
		out = (handler)(cm, event->decoded_event, client_data,
				event->attrs);
		CManager_lock(cm);
		cm->evp->current_event_item = NULL;
		if (act->action_type == Action_Filter) {
		    if (out) {
			CMtrace_out(cm, EVerbose, "Filter passed event to stone %d, submitting", term->target_stone_id);
			internal_path_submit(cm, 
					     term->target_stone_id,
					     event);
		    } else {
			CMtrace_out(cm, EVerbose, "Filter discarded event");
		    }			    
		    more_pending++;
		} else {
		    CMtrace_out(cm, EVerbose, "Finish terminal event");
		}
		return_event(evp, event);
		break;
	    }
	    case Action_Split: {
		int t = 0;
		proto_action *p = &stone->proto_actions[act->proto_action_id];
		update_event_length_sum(cm, p, event);
		while (p->o.split_stone_targets[t] != -1) {
		    internal_path_submit(cm, 
					 p->o.split_stone_targets[t],
					 event);
		    t++;
		    more_pending++;
		}
		return_event(evp, event);
		break;
	    }
	    case Action_Immediate: {
		EVImmediateHandlerFunc func;
		void *client_data;
		int *out_stones;
		int out_count;
		proto_action *p = &stone->proto_actions[act->proto_action_id];
		/* data is already in the right format */
		func = act->o.imm.handler;
		client_data = act->o.imm.client_data;
		out_stones = p->o.imm.output_stone_ids;
		out_count = p->o.imm.output_count;
		func(cm, event, client_data, event->attrs, out_count, out_stones);
		return_event(evp, event);
		more_pending++;   /* maybe??? */
		break;
	    }
	    case Action_Output:
		/* handled later */
		break;
	    default:
		assert(FALSE);
	    }
	}
	
	for (a=0 ; a < evp->stone_map[s].response_cache_count && evp->stone_map[s].is_draining == 0; a++) {

	    response_cache_element *resp = &evp->stone_map[s].response_cache[a];
	    proto_action *act;
	    if (resp->action_type == Action_NoAction) continue;
	    act = &evp->stone_map[s].proto_actions[resp->proto_action_id];
	    if (act->queue->queue_head != NULL) {
		switch (resp->action_type) {
		case Action_Terminal:
		case Action_Filter: {
		    /* the data should already be in the right format */
		    int action_id;
		    event_item *event = dequeue_event(cm, act->queue, 
						      &action_id);
		    int proto = resp->proto_action_id;
		    int out;
		    struct terminal_proto_vals *term = 
			&evp->stone_map[s].proto_actions[proto].o.term;
		    EVSimpleHandlerFunc handler = term->handler;
		    void *client_data = term->client_data;
		    CMtrace_out(cm, EVerbose, "Executing terminal/filter event");
		    update_event_length_sum(cm, act, event);
		    cm->evp->current_event_item = event;
		    CManager_unlock(cm);
		    out = (handler)(cm, event->decoded_event, client_data,
				    event->attrs);
		    CManager_lock(cm);
		    cm->evp->current_event_item = NULL;
		    if (act->action_type == Action_Filter) {
			if (out) {
			    CMtrace_out(cm, EVerbose, "Filter passed event to stone %d, submitting", term->target_stone_id);
			    internal_path_submit(cm, 
						 term->target_stone_id,
						 event);
			} else {
			    CMtrace_out(cm, EVerbose, "Filter discarded event");
			}			    
			more_pending++;
		    } else {
			CMtrace_out(cm, EVerbose, "Finish terminal event");
		    }
		    return_event(evp, event);
		    break;
		}
		case Action_Split: {
		    int action_id;
		    event_item *event = dequeue_event(cm, act->queue, 
						      &action_id);
		    int t = 0;
		    update_event_length_sum(cm, act, event);
		    while (act->o.split_stone_targets[t] != -1) {
			internal_path_submit(cm, 
					     act->o.split_stone_targets[t],
					     event);
			t++;
			more_pending++;
		    }
		    return_event(evp, event);
		  }
		case Action_Output:
		  /* handled elsewhere */
		  break;
		case Action_Immediate:
		case Action_Decode:
		case Action_NoAction:
		  assert(0);   /* handled elsewhere, shouldn't appear here */
		  break;
		}
	    }
        }
        evp->stone_map[s].is_processing = 0;
    }
    return more_pending;
}

static void
stone_close_handler(CManager cm, CMConnection conn, void *client_data)
{
    event_path_data evp = cm->evp;
    int s = (long)client_data;  /* stone ID */
    int a = 0;
    CMtrace_out(cm, EVerbose, "Got a close for connection %lx on stone %d, shutting down",
		conn, s);
    for (a=0 ; a < evp->stone_map[s].proto_action_count; a++) {
	proto_action *act = &evp->stone_map[s].proto_actions[a];
	if ((act->action_type == Action_Output) && 
	    (act->o.out.conn == conn)) {
	    act->o.out.conn_failed = 1;
	    act->o.out.conn = NULL;
	    INT_CMConnection_close(conn);   /* dereference the connection */
	    while (act->queue->queue_head != NULL) {
		int action_id;
		event_item *event = dequeue_event(cm, act->queue, &action_id);
		return_event(evp, event);
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
        if (evp->stone_map[s].is_frozen || evp->stone_map[s].is_draining) continue;
	evp->stone_map[s].is_outputting = 1;
	for (a=0 ; a < evp->stone_map[s].proto_action_count && evp->stone_map[s].is_frozen == 0 && evp->stone_map[s].is_draining == 0; a++) {
	    proto_action *act = &evp->stone_map[s].proto_actions[a];
	    if ((act->action_type == Action_Output) && 
		(act->queue->queue_head != NULL)) {
		int action_id, ret = 1;
		event_item *event = dequeue_event(cm, act->queue, &action_id);
		if (act->o.out.conn == NULL) {
		    CMtrace_out(cm, EVerbose, "Output stone %d has closed connection", s);
		} else {
		    CMtrace_out(cm, EVerbose, "Writing event to remote stone %d",
				act->o.out.remote_stone_id);
		    if (event->format) {
			ret = internal_write_event(act->o.out.conn, event->format,
						   &act->o.out.remote_stone_id, 4, 
						   event, event->attrs);
		    } else {
			struct _CMFormat tmp_format;
			if (event->reference_format == NULL) {
			    CMtrace_out(cm, EVWarning, "Tried to output event with NULL reference format.  Event discarded.\n");
			} else {
			    tmp_format.format = event->reference_format;
			    tmp_format.format_name = name_of_IOformat(event->reference_format);
			    tmp_format.IOsubcontext = (IOContext) iofile_of_IOformat(event->reference_format);
			    tmp_format.registration_pending = 0;
			    ret = internal_write_event(act->o.out.conn, &tmp_format,
						       &act->o.out.remote_stone_id, 4, 
						       event, event->attrs);
			}
		    }
		}
		return_event(evp, event);
		if (ret == 0) {
		    if (CMtrace_on(cm, EVWarning)) {
			printf("Warning!  Write failed for output action %d on stone %d, event likely not transmitted\n", a, s);
			printf("   -  Output Stone %d disabled\n", s);
		    }
		    if (act->o.out.conn != NULL) 
			INT_CMConnection_close(act->o.out.conn);
		    act->o.out.conn_failed = 1;
		    act->o.out.conn = NULL;
		}
	    }
	}
	evp->stone_map[s].is_outputting = 0;
    }	    
    return 1;
}

extern EVaction
INT_EVassoc_mutated_imm_action(CManager cm, EVstone stone_id, EVaction act_num,
			   EVImmediateHandlerFunc func, void *client_data, 
			   IOFormat reference_format)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_id];
    int resp_num = stone->response_cache_count;
    stone->response_cache = realloc(stone->response_cache, sizeof(stone->response_cache[0]) * (resp_num + 1));
    response_cache_element *resp = &stone->response_cache[stone->response_cache_count];
    resp->action_type = Action_Immediate;
    resp->requires_decoded = 1;
    resp->proto_action_id = act_num;
    resp->o.imm.handler = func;
    resp->o.imm.client_data = client_data;
    resp->reference_format = reference_format;
    stone->response_cache_count++;
    return resp_num;
}

extern EVstone
INT_EVcreate_output_action(CManager cm, attr_list contact_list,
		      EVstone remote_stone)
{
    EVstone stone = INT_EValloc_stone(cm);
    INT_EVassoc_output_action(cm, stone, contact_list, remote_stone);
    return stone;
}

extern EVaction
INT_EVassoc_output_action(CManager cm, EVstone stone_num, attr_list contact_list,
		      EVstone remote_stone)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    int action_num = stone->proto_action_count;
    CMConnection conn = INT_CMget_conn(cm, contact_list);

    if (conn == NULL) {
	if (CMtrace_on(cm, EVWarning)) {
	    printf("EVassoc_output_action - failed to contact host at contact point \n\t");
	    if (contact_list != NULL) {
		dump_attr_list(contact_list);
	    } else {
		printf("NULL\n");
	    }
	    printf("Output action association failed for stone %d, outputting to remote stone %d\n",
		   stone_num, remote_stone);
	}
	return -1;
    }
    INT_CMconn_register_close_handler(conn, stone_close_handler, 
				      (void*)(long)stone_num);
    CMtrace_out(cm, EVerbose, "Adding output action %d to stone %d",
		action_num, stone_num);
    stone->proto_actions = realloc(stone->proto_actions, 
				   (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].queue = malloc(sizeof(queue_struct));
    stone->proto_actions[action_num].queue->queue_tail = 
	stone->proto_actions[action_num].queue->queue_head = NULL;
    stone->proto_actions[action_num].action_type = Action_Output;
    stone->proto_actions[action_num].o.out.conn = conn;
    stone->proto_actions[action_num].o.out.remote_stone_id = remote_stone;
    stone->default_action = action_num;
    stone->proto_action_count++;
    clear_response_cache(stone);
    return action_num;
}

extern EVstone
INT_EVcreate_split_action(CManager cm, EVstone *target_stone_list)
{
    EVstone stone = INT_EValloc_stone(cm);
    (void) INT_EVassoc_split_action(cm, stone, target_stone_list);
    return stone;
}

extern EVaction
INT_EVassoc_split_action(CManager cm, EVstone stone_num, 
		     EVstone *target_stone_list)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    int action_num = stone->proto_action_count;
    int target_count = 0, i;
    stone->proto_actions = realloc(stone->proto_actions, 
				   (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].action_type = Action_Split;
    stone->proto_actions[action_num].queue = stone->queue;
    while (target_stone_list && (target_stone_list[target_count] != -1)) {
	target_count++;
    }
    if (CMtrace_on(cm, EVerbose)) {
	printf("Adding Split action %d to stone %d, %d target stones -> ",
		action_num, stone_num, target_count);
	for (i=0; i < target_count; i++) {
	    printf("%d, ", target_stone_list[i]);
	}
	printf("\n");
    }
    stone->proto_actions[action_num].o.split_stone_targets = 
	malloc((target_count + 1) * sizeof(EVstone));
    for (i=0; i < target_count; i++) {
	stone->proto_actions[action_num].o.split_stone_targets[i] = 
	    target_stone_list[i];
    }
    stone->proto_actions[action_num].o.split_stone_targets[i] = -1;
    stone->default_action = action_num;
    stone->proto_action_count++;
    clear_response_cache(stone);
    return action_num;
}

extern int
INT_EVaction_add_split_target(CManager cm, EVstone stone_num, 
			  EVaction action_num, EVstone new_stone_target)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    EVstone *target_stone_list;
    int target_count = 0;
    if (stone->proto_actions[action_num].action_type != Action_Split ) {
	printf("Not split action\n");
	return 0;
    }
    target_stone_list = stone->proto_actions[action_num].o.split_stone_targets;
    while (target_stone_list && (target_stone_list[target_count] != -1)) {
	target_count++;
    }
    target_stone_list = realloc(target_stone_list, 
				(target_count + 2) * sizeof(EVstone));
    target_stone_list[target_count] = new_stone_target;
    target_stone_list[target_count+1] = -1;
    stone->proto_actions[action_num].o.split_stone_targets = target_stone_list;
    return 1;
}

extern void
INT_EVaction_remove_split_target(CManager cm, EVstone stone_num, 
			  EVaction action_num, EVstone stone_target)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    EVstone *target_stone_list;
    int target_count = 0;
    if (stone->proto_actions[action_num].action_type != Action_Split ) {
	printf("Not split action\n");
    }
    target_stone_list = stone->proto_actions[action_num].o.split_stone_targets;
    if (!target_stone_list) return;
    while (target_stone_list[target_count] != stone_target) {
	target_count++;
    }
    while (target_stone_list[target_count+1] != -1 ) {
	/* move them down, overwriting target */
	target_stone_list[target_count] = target_stone_list[target_count+1];
	target_count++;
    }
    target_stone_list[target_count] = -1;
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

extern IOFormat
EVregister_format_set(CManager cm, CMFormatList list, IOContext *context_ptr)
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

static void
EVauto_submit_func(CManager cm, void* vstone)
{
    int stone_num = (long) vstone;
    event_item *event;
    CManager_lock(cm);
    event = get_free_event(cm->evp);
    event->event_encoded = 0;
    event->decoded_event = NULL;
    event->reference_format = NULL;
    event->format = NULL;
    event->free_func = NULL;
    event->attrs = NULL;
    internal_path_submit(cm, stone_num, event);
    while (process_local_actions(cm));
    process_output_actions(cm);
    return_event(cm->evp, event);
    CManager_unlock(cm);
}

extern EVstone
INT_EVcreate_auto_stone(CManager cm, int period_sec, int period_usec, 
			char *action_spec, EVstone out_stone)
{
    EVstone stone = INT_EValloc_stone(cm);
    EVaction action = INT_EVassoc_immediate_action(cm, stone, action_spec, NULL);
    INT_EVaction_set_output(cm, stone, action, 0, out_stone);
    INT_EVenable_auto_stone(cm, stone, period_sec, period_usec);
    return stone;
}

extern void
INT_EVenable_auto_stone(CManager cm, EVstone stone_num, int period_sec, 
		    int period_usec)
{
    CMTaskHandle handle = INT_CMadd_periodic_task(cm, period_sec, period_usec,
					      EVauto_submit_func, 
					      (void*)(long)stone_num);
    stone_type stone = &cm->evp->stone_map[stone_num];
    stone->periodic_handle = handle;
    CMtrace_out(cm, EVerbose, "Enabling auto events on stone %d", stone_num);
}



extern EVsource
INT_EVcreate_submit_handle(CManager cm, EVstone stone, CMFormatList data_format)
{
    EVsource source = malloc(sizeof(*source));
    memset(source, 0, sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    source->preencoded = 0;
    if (data_format != NULL) {
	source->format = INT_CMregister_format(cm, data_format[0].format_name,
					   data_format[0].field_list,
					   data_format);
	source->reference_format = EVregister_format_set(cm, data_format, NULL);
    };
    return source;
}

extern EVsource
INT_EVcreate_submit_handle_free(CManager cm, EVstone stone, 
			    CMFormatList data_format, 
			    EVFreeFunction free_func, void *free_data)
{
    EVsource source = malloc(sizeof(*source));
    memset(source, 0, sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    source->format = INT_CMregister_format(cm, data_format[0].format_name,
					    data_format[0].field_list, data_format);
    source->reference_format = EVregister_format_set(cm, data_format, NULL);
    source->free_func = free_func;
    source->free_data = free_data;
    source->preencoded = 0;
    return source;
}

extern void
INT_EVfree_source(EVsource source)
{
    free(source);
}

static void
reference_event(event_item *event)
{
    event->ref_count++;
}

extern void
internal_cm_network_submit(CManager cm, CMbuffer cm_data_buf, 
			   attr_list attrs, CMConnection conn, 
			   void *buffer, int length, int stone_id)
{
    event_path_data evp = cm->evp;
    event_item *event = get_free_event(evp);
    event->contents = Event_CM_Owned;
    event->event_encoded = 1;
    event->event_len = length;
    event->encoded_event = buffer;
    event->reference_format = get_format_app_IOcontext(evp->root_context, 
					     buffer, conn);
    event->attrs = CMadd_ref_attr_list(cm, attrs);
    event->format = NULL;
    CMtrace_out(cm, EVerbose, "Event coming in from network to stone %d", 
		stone_id);
    if (CMtrace_on(conn->cm, EVerbose)) {
	static int dump_char_limit = 256;
	static int warned = 0;
	static int size_set = 0;
	int r;
	if (size_set == 0) {
	    char *size_str = cercs_getenv("CMDumpSize");
	    size_set++;
	    if (size_str != NULL) {
		dump_char_limit = atoi(size_str);
	    }
	}
	printf("CM - record contents are:\n  ");
	r = dump_limited_encoded_IOrecord((IOFile)evp->root_context, 
					  event->reference_format,
					  event->encoded_event, dump_char_limit);
	if (r && !warned) {
	    printf("\n\n  ****  Warning **** CM record dump truncated\n");
	    printf("  To change size limits, set CMDumpSize environment variable.\n\n\n");
	    warned++;
	}
    }
    INT_CMtake_buffer(cm, buffer);
    event->cm = cm;
    internal_path_submit(cm, stone_id, event);
    return_event(evp, event);
    while (process_local_actions(cm));
    process_output_actions(cm);
}

extern void
INT_EVsubmit_general(EVsource source, void *data, EVFreeFunction free_func, 
		 attr_list attrs)
{
    event_item *event = get_free_event(source->cm->evp);
    event->contents = Event_App_Owned;
    event->event_encoded = 0;
    event->decoded_event = data;
    event->reference_format = source->reference_format;
    event->format = source->format;
    event->free_func = free_func;
    event->free_arg = data;
    internal_path_submit(source->cm, source->local_stone_id, event);
    while (process_local_actions(source->cm));
    process_output_actions(source->cm);
    if (event->ref_count != 1) {
	encode_event(source->cm, event);  /* reassign memory */
    }
    return_event(source->cm->evp, event);
}
    
void
INT_EVsubmit(EVsource source, void *data, attr_list attrs)
{
    event_path_data evp = source->cm->evp;
    event_item *event = get_free_event(evp);
    if (source->free_func != NULL) {
	event->contents = Event_Freeable;
    } else {
	event->contents = Event_App_Owned;
    }
    if (source->preencoded) {
	event->event_encoded = 1;
	event->encoded_event = data;
	event->reference_format = get_format_app_IOcontext(evp->root_context, 
							   data, NULL);
    } else {
	event->event_encoded = 0;
	event->decoded_event = data;
	event->reference_format = source->reference_format;
	event->format = source->format;
    }
    event->free_func = source->free_func;
    event->free_arg = source->free_data;
    event->attrs = CMadd_ref_attr_list(source->cm, attrs);
    internal_path_submit(source->cm, source->local_stone_id, event);
    while (process_local_actions(source->cm));
    process_output_actions(source->cm);
    if (event->ref_count != 1) {
	encode_event(source->cm, event);  /* reassign memory */
    }
    return_event(source->cm->evp, event);
}

void
INT_EVsubmit_encoded(CManager cm, EVstone stone, void *data, int data_len, attr_list attrs)
{
    event_path_data evp = cm->evp;
    event_item *event = get_free_event(evp);
    event->contents = Event_App_Owned;
    event->event_encoded = 1;
    event->encoded_event = data;
    event->event_len = data_len;
    event->reference_format = get_format_app_IOcontext(evp->root_context, 
						       data, NULL);

    event->attrs = CMadd_ref_attr_list(cm, attrs);
    internal_path_submit(cm, stone, event);
    while (process_local_actions(cm));
    process_output_actions(cm);
    if (event->ref_count != 1) {
	printf("Bad!\n");
    }
    return_event(cm->evp, event);
}

static void
free_evp(CManager cm, void *not_used)
{
    event_path_data evp = cm->evp;
    int s;
    CMtrace_out(cm, CMFreeVerbose, "Freeing evpath information, evp %lx", (long) evp);
    for (s = 0 ; s < evp->stone_count; s++) {
	INT_EVfree_stone(cm, s);
    }
    cm->evp = NULL;
    if (evp == NULL) return;
    free(evp->stone_map);
    free_IOcontext(evp->root_context);
    while (evp->queue_items_free_list != NULL) {
	queue_item *tmp = evp->queue_items_free_list->next;
	free(evp->queue_items_free_list);
	evp->queue_items_free_list = tmp;
    }
    thr_mutex_free(evp->lock);
}

void
EVPinit(CManager cm)
{
    cm->evp = INT_CMmalloc(sizeof( struct _event_path_data));
    memset(cm->evp, 0, sizeof( struct _event_path_data));
    cm->evp->root_context = create_IOsubcontext(cm->IOcontext);
    cm->evp->queue_items_free_list = NULL;
    cm->evp->lock = thr_mutex_alloc();
    internal_add_shutdown_task(cm, free_evp, NULL);
}

    
extern int
INT_EVtake_event_buffer(CManager cm, void *event)
{
    queue_item *item;
    event_item *cur = cm->evp->current_event_item;
    event_path_data evp = cm->evp;

    if (cur == NULL) {
	fprintf(stderr,
		"No event handler with takeable buffer executing on this CM.\n");
	return 0;
    }
    if (!((cur->decoded_event <= event) &&
	  ((char *) event <= ((char *) cur->decoded_event + cur->event_len)))){
	fprintf(stderr,
		"Event address (%lx) in INT_EVtake_event_buffer does not match currently executing event on this CM.\n",
		(long) event);
	return 0;
    }
/*    if (cur->block_rec == NULL) {
	static int take_event_warning = 0;
	if (take_event_warning == 0) {
	    fprintf(stderr,
		    "Warning:  INT_EVtake_event_buffer called on an event submitted with \n    INT_EVsubmit_event(), INT_EVsubmit_typed_event() or INT_EVsubmit_eventV() .\n    This violates ECho event data memory handling requirements.  See \n    http://www.cc.gatech.edu/systems/projects/ECho/event_memory.html\n");
	    take_event_warning++;
	}
	return 0;
    }
*/
    if (evp->queue_items_free_list == NULL) {
	item = malloc(sizeof(*item));
    } else {
	item = evp->queue_items_free_list;
	evp->queue_items_free_list = item->next;
    }
    item->item = cur;
    reference_event(cm->evp->current_event_item);
    item->next = evp->taken_events_list;
    evp->taken_events_list = item;
    return 1;
}

void
INT_EVreturn_event_buffer(cm, event)
CManager cm;
void *event;
{
    event_path_data evp = cm->evp;
    queue_item *tmp, *last = NULL;
    /* search through list for event and then dereference it */

    tmp = evp->taken_events_list;
    while (tmp != NULL) {
	if ((tmp->item->decoded_event <= event) &&
	    ((char *) event <= ((char *) tmp->item->decoded_event + tmp->item->event_len))) {
	    if (last == NULL) {
		evp->taken_events_list = tmp->next;
	    } else {
		last->next = tmp->next;
	    }
	    return_event(cm->evp, event);
	    tmp->next = evp->queue_items_free_list;
	    evp->queue_items_free_list = tmp;
	    return;
	}
	last = tmp;
	tmp = tmp->next;
    }
    fprintf(stderr, "Event %lx not found in taken events list\n",
	    (long) event);
}

extern IOFormat
INT_EVget_src_ref_format(EVsource source)
{
    return source->reference_format;
}

extern int
INT_EVfreeze_stone(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    if (evp->stone_count < stone_id) {
        return -1;
    }
    stone = &(evp->stone_map[stone_id]);
    stone->is_frozen = 1;
    return 1;	
}

extern int
INT_EVdrain_stone(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
/*    attr_list stone_attrs;
    EVevent_list buf_list = NULL;*/
    if (evp->stone_count < stone_id) {
        return -1;
    }
    stone = &(evp->stone_map[stone_id]);
    stone->is_draining = 1;
    while(stone->is_processing || stone->is_outputting);
/*    buf_list = EVextract_stone_events(cm, stone_id);
    stone_attrs = EVextract_attr_list(cm, stone_id); */
    stone->is_draining = 2;
    return 1;
}

extern EVevent_list
INT_EVextract_stone_events(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    EVevent_list list = malloc(sizeof(list[0]));
    int a;

    list[0].length = -1;
    stone = &(evp->stone_map[stone_id]);
    list = extract_events_from_queue(cm, stone->queue, list);
    for (a=0 ; a < evp->stone_map[stone_id].proto_action_count; a++) {
        proto_action *act = &evp->stone_map[stone_id].proto_actions[a];
        if (act->queue != stone->queue) {
            list = extract_events_from_queue(cm, act->queue, list);
        }
    }
    return list;
}

extern attr_list
INT_EVextract_attr_list(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    stone = &(evp->stone_map[stone_id]);
    return(stone->stone_attrs);
}

EVevent_list
extract_events_from_queue(CManager cm, queue_ptr que, EVevent_list list)
{
    EVevent_list current_entry = NULL;
    queue_item *first = NULL, *last = NULL;
    int num_of_elements = 0;
    
    first = que->queue_head;
    last = que->queue_tail;
                
    while (list[num_of_elements].length != -1) num_of_elements++;
    while(first != NULL && last != NULL) {
	list = (EVevent_list) realloc (list, (num_of_elements + 2) * sizeof(list[0]));
	current_entry = &list[num_of_elements];
	if((first->item->event_encoded) || (first->item->ioBuffer != NULL)) {
	    current_entry->length = first->item->event_len;
	    current_entry->buffer = first->item->encoded_event;
	} else {
	    encode_event(cm, first->item);
	    current_entry->length = first->item->event_len;
	    current_entry->buffer = first->item->encoded_event;
	}
	num_of_elements++;
	first = first->next;
    }
    list[num_of_elements].length = -1;
    return list;
}


extern int
INT_EVdestroy_stone(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    if (evp->stone_count < stone_id) {
        return -1;
    }
    stone = &(evp->stone_map[stone_id]);
    while (stone->is_draining != 2);  
    while(stone->queue->queue_head != NULL && stone->queue->queue_tail != NULL) {
        free(stone->queue->queue_head->item->ioBuffer);
        if(stone->queue->queue_head == stone->queue->queue_tail) {
	    stone->queue->queue_head = NULL;
	    stone->queue->queue_tail = NULL;
	}       
	else
	    stone->queue->queue_head = stone->queue->queue_head->next;
    }    
    if (stone->response_cache) free(stone->response_cache);
    INT_EVfree_stone(cm, stone_id);  
    return 1;      
} 
    

