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
static void return_event(event_path_data evp, event_item *event);
static event_item *get_free_event(event_path_data evp);
static void dump_action(stone_type stone, int a, const char *indent);
extern void print_server_ID(char *server_id);
static void dump_stone(stone_type stone);

static const char *action_str[] = { "Action_Output", "Action_Terminal", "Action_Filter", "Action_Immediate", "Action_Decode", "Action_Split"};

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
    stone->queue = malloc(sizeof(queue_struct));
    stone->queue->queue_tail = stone->queue->queue_head = NULL;
    stone->proto_actions = NULL;
    return stone_num;
}

void
EVfree_stone(CManager cm, EVstone stone_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int i;

    stone = &evp->stone_map[stone_num];
    if (stone->local_id == -1) return;
    if (stone->periodic_handle != NULL) {
	CMremove_task(stone->periodic_handle);
	stone->periodic_handle = NULL;
    }
    for(i = 0; i < stone->action_count; i++) {
	action *act = &stone->actions[i];
	if (act->attrs != NULL) {
	    CMfree_attr_list(cm, act->attrs);
	}
	switch(act->action_type) {
	case Action_Output:
	    if (act->o.out.remote_path) 
		free(act->o.out.remote_path);
	    break;
	case Action_Terminal:
	    break;
	case Action_Filter:
	    break;
	case Action_Decode:
	    free_IOcontext(act->o.decode.context);
	    break;
	case Action_Split:
	    free(act->o.split_stone_targets);
	    break;
	case Action_Immediate:
	    if (act->o.imm.mutable_response_data != NULL) {
		response_data_free(cm, act->o.imm.mutable_response_data);
	    }
	    free(act->o.imm.subacts);
	    free(act->o.imm.output_stone_ids);
	    break;
	}
    }
    free(stone->queue);
    free(stone->actions);
    free(stone->proto_actions);
    stone->local_id = -1;
}

EVstone
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
    stone->proto_actions[proto_action_num].reference_format = NULL;
    if (format_list != NULL) {
	stone->proto_actions[proto_action_num].reference_format = 
	    EVregister_format_set(cm, format_list, NULL);
    }	
    action_num = stone->action_count++;
    CMtrace_out(cm, EVerbose, "Adding Terminal action %d to stone %d",
		action_num, stone_num);
    stone->actions = realloc(stone->actions, (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, sizeof(stone->actions[0]));
    stone->actions[action_num].queue = stone->queue;
    stone->actions[action_num].action_type = Action_Terminal;
    stone->actions[action_num].requires_decoded = 1;
    stone->actions[action_num].reference_format = 
	stone->proto_actions[proto_action_num].reference_format;
    stone->actions[action_num].o.terminal_proto_action_number = proto_action_num;
    return action_num;
}
    

EVaction
EVassoc_immediate_action(CManager cm, EVstone stone_num, 
			 char *action_spec, void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone = &evp->stone_map[stone_num];

    action_num = stone->action_count++;
    CMtrace_out(cm, EVerbose, "Adding Immediate action %d to stone %d",
		action_num, stone_num);
    stone->actions = realloc(stone->actions, (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, sizeof(stone->actions[0]));
    stone->actions[action_num].requires_decoded = 1;
    stone->actions[action_num].queue = stone->queue;
    stone->actions[action_num].action_type = Action_Immediate;
    stone->actions[action_num].o.imm.subaction_count = 0;
    stone->actions[action_num].o.imm.subacts = NULL;
    stone->actions[action_num].o.imm.output_stone_ids = malloc(sizeof(int));
    stone->actions[action_num].o.imm.output_stone_ids[0] = -1;
    stone->actions[action_num].o.imm.mutable_response_data = 
	install_response_handler(cm, stone_num, action_spec, client_data);
    return action_num;
}

EVstone
EVassoc_filter_action(CManager cm, EVstone stone_num, 
		      CMFormatList format_list, EVSimpleHandlerFunc handler,
		      EVstone out_stone_num, void *client_data)
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
    stone->proto_actions[proto_action_num].action_type = Action_Filter;
    stone->proto_actions[proto_action_num].t.term.handler = handler;
    stone->proto_actions[proto_action_num].t.term.client_data = client_data;
    stone->proto_actions[proto_action_num].t.term.target_stone_id = out_stone_num;
    stone->proto_actions[proto_action_num].reference_format = NULL;
    if (format_list != NULL) {
	stone->proto_actions[proto_action_num].reference_format = 
	    EVregister_format_set(cm, format_list, NULL);
    }	
    action_num = stone->action_count++;
    stone->actions = realloc(stone->actions, (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, sizeof(stone->actions[0]));
    stone->actions[action_num].queue = stone->queue;
    stone->actions[action_num].action_type = Action_Filter;
    stone->actions[action_num].requires_decoded = 1;
    stone->actions[action_num].reference_format = 
	stone->proto_actions[proto_action_num].reference_format;
    stone->actions[action_num].o.terminal_proto_action_number = proto_action_num;
    return action_num;
}
    

static int evpath_locked(){return 1;}

static void
enqueue_event(CManager cm, int stone_id, int action_id, int sub_act, event_item *event)
{
    event_path_data evp = cm->evp;
    queue_item *item;
    stone_type stone = &evp->stone_map[stone_id];
    action *act = &stone->actions[action_id];
    if (evp->queue_items_free_list == NULL) {
	item = malloc(sizeof(*item));
    } else {
	item = evp->queue_items_free_list;
	evp->queue_items_free_list = item->next;
    }
    item->item = event;
    item->action_id = action_id;
    item->subaction_id = sub_act;
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
dequeue_event(CManager cm, queue_ptr q, int *act_p, int *subact_p)
{
    event_path_data evp = cm->evp;
    queue_item *item = q->queue_head;
    event_item *event = NULL;
    if (item == NULL) return event;
    event = item->item;
    *act_p = item->action_id;
    *subact_p = item->subaction_id;
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
    free(target_subformat_list);
}

extern void
EVassoc_conversion_action(cm, stone_id, target_format, incoming_format)
CManager cm;
int stone_id;
IOFormat target_format;
IOFormat incoming_format;
{
    stone_type stone = &(cm->evp->stone_map[stone_id]);
    int a = stone->action_count++;
    int id_len;
    IOFormat format;

    char *server_id = get_server_ID_IOformat(incoming_format,
						     &id_len);
/*	    printf("Creating new DECODE action\n");*/
    CMtrace_out(cm, EVerbose, "Adding Conversion action %d to stone %d",
		a, stone_id);
    stone->actions = realloc(stone->actions, 
			     sizeof(stone->actions[0]) * (a + 1));
    action *act = & stone->actions[a];
    memset(act, 0, sizeof(*act));
    act->requires_decoded = 0;
    act->action_type = Action_Decode;
    act->reference_format = incoming_format;
    act->queue = stone->queue;

    act->o.decode.context = create_IOsubcontext(cm->evp->root_context);
    format = get_format_app_IOcontext(act->o.decode.context, 
				      server_id, NULL);
    act->o.decode.decode_format = format;
    act->o.decode.target_reference_format = target_format;
    set_conversions(act->o.decode.context, format,
		    act->o.decode.target_reference_format);
}

int
EVaction_set_output(CManager cm, EVstone stone_num, EVaction act_num, 
		    int output_index, EVstone output_stone)
{
    stone_type stone;
    int output_count = 0;
    if (stone_num > cm->evp->stone_count) return 0;
    stone = &(cm->evp->stone_map[stone_num]);
    if (act_num > stone->action_count) return 0;
    assert(stone->actions[act_num].action_type == Action_Immediate);
    while (stone->actions[act_num].o.imm.output_stone_ids[output_count] != -1) 
	output_count++;
    stone->actions[act_num].o.imm.output_stone_ids = 
	realloc(stone->actions[act_num].o.imm.output_stone_ids,
		sizeof(int) * (output_count + 2));
    stone->actions[act_num].o.imm.output_stone_ids[output_count] = output_stone;
    stone->actions[act_num].o.imm.output_stone_ids[output_count+1] = -1;
    return 1;
}
    

static int
determine_action(CManager cm, stone_type stone, event_item *event, int *sub_id)
{
    int i;
    CMtrace_out(cm, EVerbose, "Call to determine_action, event reference_format is %lx",
	   event->reference_format);
    for (i=0; i < stone->action_count; i++) {
	if ((stone->actions[i].action_type == Action_Immediate) && 
	    (!event->event_encoded)) {
	    int j;
	    immediate_action_vals *imm = &stone->actions[i].o.imm;
	    for (j=0; j < imm->subaction_count ; j++) {
		if (imm->subacts[j].reference_format == event->reference_format) {
		    *sub_id = j;
		    return i;
		}
	    }
	}
	if (stone->actions[i].reference_format == event->reference_format) {
	    if (event->event_encoded && stone->actions[i].requires_decoded) {
		continue;
	    }
	    return i;
	}
    }
    if (response_determination(cm, stone, event) == 1) {
	return determine_action(cm, stone, event, sub_id);
    }
    if (stone->default_action != -1) {
/*	    printf(" Returning ");
	    dump_action(stone, stone->default_action, "   ");*/
	return stone->default_action;
    }
    return -1;
}

/*   GSE
 *   Need to seriously augment buffer handling.  In particular, we have to
 *   formalize the handling of event buffers.  Make sure we have all the
 *   situations covered, try to keep both encoded and decoded versions of
 *   events where possible.  Try to augment testing because many cases are
 *   not covered in homogeneous regression testing (our normal mode).
 */

static event_item *
decode_action(CManager cm, event_item *event, action *act)
{
    if (event->event_encoded) {
	if (decode_in_place_possible(act->o.decode.decode_format)) {
	    void *decode_buffer;
	    if (!decode_in_place_IOcontext(act->o.decode.context,
					   event->encoded_event, 
					   (void**) (long) &decode_buffer)) {
		printf("Decode failed\n");
		return 0;
	    }
	    event->decoded_event = decode_buffer;
	    event->event_encoded = 0;
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
	    CMtake_buffer(cm, decode_buffer);
	    event->decoded_event = decode_buffer;
	    event->event_encoded = 0;
	    event->reference_format = act->o.decode.target_reference_format;
	    return event;
	}
    } else {
	assert(0);
    }
}

static void
dump_proto_action(stone_type stone, int a, const char *indent)
{
    proto_action *proto = &stone->proto_actions[a];
    printf("Proto-Action %d - %s\n", a, action_str[proto->action_type]);
}

static void
dump_action(stone_type stone, int a, const char *indent)
{
    action *act = &stone->actions[a];
    printf("Action %d - %s  ", a, action_str[act->action_type]);
    if (act->requires_decoded) {
	printf("requires decoded\n");
    } else {
	printf("accepts encoded\n");
    }
    printf("  reference format :");
    if (act->reference_format) {
	int id_len;
	print_server_ID(get_server_ID_IOformat(act->reference_format, &id_len));
    } else {
	printf(" NULL\n");
    }
    switch(act->action_type) {
    case Action_Output:
	printf("  Target: connection %lx, remote_stone_id %d, new %d, write_pending %d\n",
	       (long)(void*)act->o.out.conn, act->o.out.remote_stone_id, 
	       act->o.out.new, act->o.out.write_pending);
	break;
    case Action_Terminal:
	printf("  Terminal proto action number %d\n",
	       act->o.terminal_proto_action_number);
	break;
    case Action_Filter:
	printf("  Filter proto action number %d\n",
	       act->o.terminal_proto_action_number);
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
	{
	    int i = 0;
	    for (i=0; i < act->o.imm.subaction_count; i++) {
		printf("      Subaction %d, ref_format %lx, handler %lx\n",
		       i, (long)act->o.imm.subacts[i].reference_format,
		       (long)act->o.imm.subacts[i].handler);
	    }
	    printf("\n");
	}
	break;
    }
}

static void
dump_stone(stone_type stone)
{
    int i;
    printf("Stone %lx, local ID %d, default action %d\n",
	   (long)stone, stone->local_id, stone->default_action);
    printf("  proto_action_count %d:\n", stone->proto_action_count);
    for (i=0; i< stone->proto_action_count; i++) {
	dump_proto_action(stone, i, "    ");
    }
    printf("  action_count %d:\n", stone->action_count);
    for (i=0; i< stone->action_count; i++) {
	dump_action(stone, i, "    ");
    }
}

int
internal_path_submit(CManager cm, int local_path_id, event_item *event)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int action_id;
    int subact = -1;
    action *act = NULL;

    assert(evpath_locked());
    if (evp->stone_count < local_path_id) {
	return -1;
    }
    stone = &(evp->stone_map[local_path_id]);
    action_id = determine_action(cm, stone, event, &subact);
    if (action_id ==  -1) {
	printf("No action found for event %lx submitted to stone %d\n",
	       (long)event, local_path_id);
	if ((stone->actions[0].action_type == Action_Terminal) ||
	    CMtrace_on(cm, EVerbose)) {
	    if (event->decoded_event != NULL) {
		dump_unencoded_IOrecord(iofile_of_IOformat(event->reference_format),
					event->reference_format,
					event->decoded_event);
	    } else {
		dump_encoded_as_XML(evp->root_context, event->encoded_event);
	    }
	}
	dump_stone(stone);
	return 0;
    }
    act = &stone->actions[action_id];
    if (act->action_type == Action_Decode) {
	CMtrace_out(cm, EVerbose, "Decoding event");
	event = decode_action(cm, event, act);
	action_id = determine_action(cm, stone, event, &subact);
    }
    if (CMtrace_on(cm, EVerbose)) {
	printf("Enqueueing event %lx on stone %d, action %lx",
	       (long)event, local_path_id, (long)act);
	dump_action(stone, action_id, "    ");
    }
    enqueue_event(cm, local_path_id, action_id, subact, event);
    return 1;
}

static void
update_event_length_sum(cm, act, event)
CManager cm;
action *act; 
event_item *event;
{
    int eventlength;
    int totallength; 

    /*update act->event_length_sum:*/
    if (query_attr(event->attrs, CM_EVENT_SIZE, NULL,
		   /* value pointer */ (attr_value *) (long)& eventlength)) {
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
    set_attr(act->attrs, EV_EVENT_LSUM, Attr_Int4, (attr_value)totallength);
}

static
int
process_local_actions(cm)
CManager cm;
{
    event_path_data evp = cm->evp;
    int s, a, more_pending = 0;
    CMtrace_out(cm, EVerbose, "Process local actions");
    for (s = 0; s < evp->stone_count; s++) {
	while (evp->stone_map[s].queue->queue_head != NULL) {
	    int action_id, subaction_id;
	    event_item *event = dequeue_event(cm, evp->stone_map[s].queue, 
					      &action_id, &subaction_id);
	    action *act = &evp->stone_map[s].actions[action_id];
#ifdef NOTDEF
	    if (act->action_type == Action_Decode) {
		CMtrace_out(cm, EVerbose, "Decoding event  %lx, stone %d, act %d",
			    event, s, action_id);
		event = decode_action(cm, event, act);
		action_id = determine_action(cm, &evp->stone_map[s], 
					     event, &subaction_id);
		act = &evp->stone_map[s].actions[action_id];
	    }
#endif
	    switch(act->action_type) {
	    case Action_Terminal:
	    case Action_Filter: {
		/* the data should already be in the right format */
		int proto = act->o.terminal_proto_action_number;
		int out;
		struct terminal_proto_vals *term = 
		    &evp->stone_map[s].proto_actions[proto].t.term;
		EVSimpleHandlerFunc handler = term->handler;
		void *client_data = term->client_data;
		CMtrace_out(cm, EVerbose, "Executing terminal/filter event");
		update_event_length_sum(cm, act, event);
		cm->evp->current_event_item = event;
		out = (handler)(cm, event->decoded_event, client_data,
				event->attrs);
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
		update_event_length_sum(cm, act, event);
		while (act->o.split_stone_targets[t] != -1) {
		    internal_path_submit(cm, 
					 act->o.split_stone_targets[t],
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
		/* data is already in the right format */
		func = act->o.imm.subacts[subaction_id].handler;
		client_data = act->o.imm.subacts[subaction_id].client_data;
		out_stones = act->o.imm.output_stone_ids;
		func(cm, event, client_data, event->attrs, out_stones);
		return_event(evp, event);
		more_pending++;   /* maybe??? */
		break;
	    }
	    default:
		assert(FALSE);
	    }
	}

	for (a=0 ; a < evp->stone_map[s].action_count; a++) {
	    action *act = &evp->stone_map[s].actions[a];
	    if (act->queue->queue_head != NULL) {
		switch (act->action_type) {
		case Action_Terminal:
		case Action_Filter: {
		    /* the data should already be in the right format */
		    int action_id, subaction_id;
		    event_item *event = dequeue_event(cm, act->queue, 
						      &action_id, &subaction_id);
		    int proto = act->o.terminal_proto_action_number;
		    int out;
		    struct terminal_proto_vals *term = 
			&evp->stone_map[s].proto_actions[proto].t.term;
		    EVSimpleHandlerFunc handler = term->handler;
		    void *client_data = term->client_data;
		    CMtrace_out(cm, EVerbose, "Executing terminal/filter event");
		    update_event_length_sum(cm, act, event);
		    cm->evp->current_event_item = event;
		    out = (handler)(cm, event->decoded_event, client_data,
				    event->attrs);
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
		    int action_id, subaction_id;
		    event_item *event = dequeue_event(cm, act->queue, 
						      &action_id,
						      &subaction_id);
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
		  assert(0);   /* handled elsewhere, shouldn't appear here */
		  break;
		}
	    }
	}
    }
    return more_pending;
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
		(act->queue->queue_head != NULL)) {
		int action_id, subact_id;
		event_item *event = dequeue_event(cm, act->queue, &action_id, 
						  &subact_id);
		CMtrace_out(cm, EVerbose, "Writing event to remote stone %d",
			    act->o.out.remote_stone_id);
		if (event->format) {
		    internal_write_event(act->o.out.conn, event->format,
					 &act->o.out.remote_stone_id, 4, 
					 event, event->attrs);
		} else {
		    struct _CMFormat tmp_format;
		    tmp_format.format = event->reference_format;
		    tmp_format.format_name = name_of_IOformat(event->reference_format);
		    tmp_format.IOsubcontext = (IOContext) iofile_of_IOformat(event->reference_format);
		    tmp_format.registration_pending = 0;
		    internal_write_event(act->o.out.conn, &tmp_format,
					 &act->o.out.remote_stone_id, 4, 
					 event, event->attrs);
		}
		return_event(evp, event);
	    }
	}
    }	    
    return 1;
}

extern EVaction
EVassoc_mutated_imm_action(CManager cm, EVstone stone_id, EVaction act_num,
			   EVImmediateHandlerFunc func, void *client_data, 
			   IOFormat reference_format)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_id];
    action *act = &stone->actions[act_num];
    int sub_num = act->o.imm.subaction_count++;
    
    assert(act->action_type == Action_Immediate);
    act->o.imm.subacts = realloc(act->o.imm.subacts, 
				 (sub_num + 1) * sizeof(immediate_sub));
    act->o.imm.subacts[sub_num].handler = func;
    act->o.imm.subacts[sub_num].client_data = client_data;
    act->o.imm.subacts[sub_num].reference_format = reference_format;
    return sub_num;
}


extern EVaction
EVassoc_output_action(CManager cm, EVstone stone_num, attr_list contact_list,
		      EVstone remote_stone)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    int action_num = stone->action_count++;
    CMtrace_out(cm, EVerbose, "Adding output action %d to stone %d",
		action_num, stone_num);
    stone->actions = realloc(stone->actions, 
				   (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, 
	   sizeof(stone->actions[0]));
    stone->actions[action_num].queue = malloc(sizeof(queue_struct));
    stone->actions[action_num].queue->queue_tail = 
	stone->actions[action_num].queue->queue_head = NULL;
    stone->actions[action_num].action_type = Action_Output;
    stone->actions[action_num].o.out.conn = CMget_conn(cm, contact_list);
    stone->actions[action_num].o.out.remote_stone_id = remote_stone;
    stone->default_action = action_num;
    return action_num;
}

extern EVaction
EVassoc_split_action(CManager cm, EVstone stone_num, 
		     EVstone *target_stone_list)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    int action_num = stone->action_count++;
    int target_count = 0, i;
    CMtrace_out(cm, EVerbose, "Adding Split action %d to stone %d",
		action_num, stone_num);
    stone->actions = realloc(stone->actions, 
				   (action_num + 1) * 
				   sizeof(stone->actions[0]));
    memset(&stone->actions[action_num], 0, 
	   sizeof(stone->actions[0]));
    stone->actions[action_num].action_type = Action_Split;
    stone->actions[action_num].queue = stone->queue;
    while (target_stone_list && (target_stone_list[target_count] != -1)) {
	target_count++;
    }
    stone->actions[action_num].o.split_stone_targets = 
	malloc((target_count + 1) * sizeof(EVstone));
    for (i=0; i < target_count; i++) {
	stone->actions[action_num].o.split_stone_targets[i] = 
	    target_stone_list[i];
    }
    stone->actions[action_num].o.split_stone_targets[i] = -1;
    stone->default_action = action_num;
    return action_num;
}

extern int
EVaction_add_split_target(CManager cm, EVstone stone_num, 
			  EVaction action_num, EVstone new_stone_target)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    EVstone *target_stone_list;
    int target_count = 0;
    if (stone->actions[action_num].action_type != Action_Split ) {
	printf("Not split action\n");
	return 0;
    }
    target_stone_list = stone->actions[action_num].o.split_stone_targets;
    while (target_stone_list && (target_stone_list[target_count] != -1)) {
	target_count++;
    }
    target_stone_list = realloc(target_stone_list, 
				(target_count + 2) * sizeof(EVstone));
    target_stone_list[target_count] = new_stone_target;
    target_stone_list[target_count+1] = -1;
    stone->actions[action_num].o.split_stone_targets = target_stone_list;
    return 1;
}

extern void
EVaction_remove_split_target(CManager cm, EVstone stone_num, 
			  EVaction action_num, EVstone stone_target)
{
    event_path_data evp = cm->evp;
    stone_type stone = &evp->stone_map[stone_num];
    EVstone *target_stone_list;
    int target_count = 0;
    if (stone->actions[action_num].action_type != Action_Split ) {
	printf("Not split action\n");
    }
    target_stone_list = stone->actions[action_num].o.split_stone_targets;
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
    event_item *event = get_free_event(cm->evp);
    event->event_encoded = 0;
    event->decoded_event = NULL;
    event->reference_format = NULL;
    event->format = NULL;
    event->free_func = NULL;
    event->attrs = NULL;
    internal_path_submit(cm, stone_num, event);
    return_event(cm->evp, event);
    while (process_local_actions(cm));
    process_output_actions(cm);
}

extern void
EVenable_auto_stone(CManager cm, EVstone stone_num, int period_sec, 
		    int period_usec)
{
    CMTaskHandle handle = CMadd_periodic_task(cm, period_sec, period_usec,
					      EVauto_submit_func, 
					      (void*)(long)stone_num);
    stone_type stone = &cm->evp->stone_map[stone_num];
    stone->periodic_handle = handle;
}



extern EVsource
EVcreate_submit_handle(CManager cm, EVstone stone, CMFormatList data_format)
{
    EVsource source = malloc(sizeof(*source));
    memset(source, 0, sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    if (data_format != NULL) {
	source->format = CMregister_format(cm, data_format[0].format_name,
					   data_format[0].field_list,
					   data_format);
	source->reference_format = EVregister_format_set(cm, data_format, NULL);
    };
    return source;
}

extern EVsource
EVcreate_submit_handle_free(CManager cm, EVstone stone, 
			    CMFormatList data_format, 
			    EVFreeFunction free_func, void *free_data)
{
    EVsource source = malloc(sizeof(*source));
    memset(source, 0, sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    source->format = CMregister_format(cm, data_format[0].format_name,
					    data_format[0].field_list, data_format);
    source->reference_format = EVregister_format_set(cm, data_format, NULL);
    source->free_func = free_func;
    source->free_data = free_data;
    return source;
}

static event_item *
get_free_event(event_path_data evp)
{
    event_item *event = malloc(sizeof(*event));
    memset(event, 0, sizeof(event_item));
    event->ref_count = 1;
    event->event_len = -1;
    return event;
}

static void
return_event(event_path_data evp, event_item *event)
{
    event->ref_count--;
    if (event->ref_count == 0) {
	/* return event memory */
	switch (event->contents) {
	case Event_CM_Owned:
	    CMreturn_buffer(event->cm, event->decoded_event);
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
	if (event->attrs != NULL) CMfree_attr_list(event->cm, event->attrs);
	free(event);
    }
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
    CMtake_buffer(cm, buffer);
    event->cm = cm;
    internal_path_submit(cm, stone_id, event);
    return_event(evp, event);
    while (process_local_actions(cm));
    process_output_actions(cm);
}

extern void
EVsubmit_general(EVsource source, void *data, EVFreeFunction free_func, 
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
    return_event(source->cm->evp, event);
    while (process_local_actions(source->cm));
    process_output_actions(source->cm);
}
    
void
EVsubmit(EVsource source, void *data, attr_list attrs)
{
    event_item *event = get_free_event(source->cm->evp);
    if (source->free_func != NULL) {
	event->contents = Event_Freeable;
    } else {
	event->contents = Event_App_Owned;
    }
    event->event_encoded = 0;
    event->decoded_event = data;
    event->reference_format = source->reference_format;
    event->format = source->format;
    event->free_func = source->free_func;
    event->free_arg = source->free_data;
    event->attrs = CMadd_ref_attr_list(source->cm, attrs);
    internal_path_submit(source->cm, source->local_stone_id, event);
    return_event(source->cm->evp, event);
    while (process_local_actions(source->cm));
    process_output_actions(source->cm);
}

static void
free_evp(CManager cm, void *not_used)
{
    event_path_data evp = cm->evp;
    int s;
    for (s = 0 ; s < evp->stone_count; s++) {
	EVfree_stone(cm, s);
    }
    cm->evp = NULL;
    if (evp == NULL) return;
    free(evp->stone_map);
    free(evp->output_actions);
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
    cm->evp = CMmalloc(sizeof( struct _event_path_data));
    memset(cm->evp, 0, sizeof( struct _event_path_data));
    cm->evp->root_context = create_IOcontext();
    cm->evp->queue_items_free_list = NULL;
    cm->evp->lock = thr_mutex_alloc();
    internal_add_shutdown_task(cm, free_evp, NULL);
}

    
extern int
EVtake_event_buffer(CManager cm, void *event)
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
		"Event address (%lx) in EVtake_event_buffer does not match currently executing event on this CM.\n",
		(long) event);
	return 0;
    }
/*    if (cur->block_rec == NULL) {
	static int take_event_warning = 0;
	if (take_event_warning == 0) {
	    fprintf(stderr,
		    "Warning:  EVtake_event_buffer called on an event submitted with \n    EVsubmit_event(), EVsubmit_typed_event() or EVsubmit_eventV() .\n    This violates ECho event data memory handling requirements.  See \n    http://www.cc.gatech.edu/systems/projects/ECho/event_memory.html\n");
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
EVreturn_event_buffer(cm, event)
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
EVget_src_ref_format(EVsource source)
{
    return source->reference_format;
}
