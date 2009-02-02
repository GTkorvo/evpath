
#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>
#include <unistd.h>

#include "evpath.h"
#include "cm_internal.h"
#include "response.h"

extern 
FMFormat EVregister_format_set(CManager cm, FMStructDescList list);
static void reference_event(event_item *event);
static void dump_action(stone_type stone, response_cache_element *resp, 
			int a, const char *indent);
static void dump_stone(stone_type stone);
static int is_output_stone(CManager cm, EVstone stone_num);

static const char *action_str[] = { "Action_NoAction","Action_Output", "Action_Terminal", "Action_Filter", "Action_Immediate", "Action_Multi", "Action_Decode", "Action_Encode_to_Buffer", "Action_Split", "Action_Store", "Action_Congestion"};

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

stone_type
stone_struct(event_path_data evp, int stone_num)
{
    if (evp->stone_count < stone_num - evp->stone_base_num) {
	printf("EVPATH: Invalid stone ID %d\n", stone_num);
        return NULL;
    }
    return &evp->stone_map[stone_num - evp->stone_base_num];
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
    stone_num += evp->stone_base_num;
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
    stone->new_enqueue_flag = 0;
    stone->write_callback = -1;
    stone->proto_actions = NULL;
    stone->stone_attrs = CMcreate_attr_list(cm);
    stone->queue_size = 0;
    stone->is_stalled = 0;
    stone->stall_from = Stall_None;
    stone->last_remote_source = NULL;
    stone->squelch_depth = 0;
    stone->unstall_callbacks = NULL;
    evp->stone_count++;
    return stone_num;
}
static void
empty_queue(queue_ptr queue)
{
    while(queue->queue_head != NULL && queue->queue_tail != NULL) {
        free(queue->queue_head->item->ioBuffer);
        if(queue->queue_head == queue->queue_tail) {
	    queue->queue_head = NULL;
	    queue->queue_tail = NULL;
	}       
	else
	    queue->queue_head = queue->queue_head->next;
    }
}

/* {{{ storage_queue_* */
static storage_queue_ptr 
storage_queue_init(CManager cm, storage_queue_ptr queue,
        storage_queue_ops_ptr ops, attr_list attrs) {
    memset(queue, 0, sizeof *queue);
    queue->ops = ops;
    if (queue->ops->init)
        (queue->ops->init)(cm, queue, attrs);
    return queue;
}

static void
storage_queue_cleanup(CManager cm, storage_queue_ptr queue) {
    if (queue->ops->cleanup)
        (queue->ops->cleanup)(cm, queue);
}

static void
storage_queue_empty(CManager cm, storage_queue_ptr queue) {
    (queue->ops->empty)(cm, queue);
}

static void
storage_queue_enqueue(CManager cm, storage_queue_ptr queue, event_item *item) {
    (queue->ops->enqueue)(cm, queue, item);
}

static event_item *
storage_queue_dequeue(CManager cm, storage_queue_ptr queue) {
    return (queue->ops->dequeue)(cm, queue);
}
/* }}} */

void
INT_EVfree_stone(CManager cm, EVstone stone_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int i;

    stone = stone_struct(evp, stone_num);
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
	if (act->matching_reference_formats != NULL) 
	    free(act->matching_reference_formats);
	switch(act->action_type) {
	case Action_NoAction:
	case Action_Encode_to_Buffer:
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
		free_FFSContext(act->o.decode.context);
		act->o.decode.context = NULL;
	    }
	    break;
	case Action_Split:
	    free(act->o.split_stone_targets);
	    break;
	case Action_Immediate:
        case Action_Multi:
        case Action_Congestion:
	    if (act->o.imm.mutable_response_data != NULL) {
		response_data_free(cm, act->o.imm.mutable_response_data);
	    }
	    free(act->o.imm.output_stone_ids);
	    break;
        case Action_Store:
            storage_queue_cleanup(cm, &act->o.store.queue);
            break; 
	}
    }
    if (stone->proto_actions != NULL) free(stone->proto_actions);
    for(i = 0; i < stone->response_cache_count; i++) {
	response_cache_element *resp = &stone->response_cache[i];
	switch(resp->action_type) {
	case Action_Decode:
	    if (resp->o.decode.context) {
		free_FFSContext(resp->o.decode.context);
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
    /* XXX unsquelch senders */
    stone->queue = NULL;
    stone->local_id = -1;
    stone->proto_action_count = 0;
    stone->proto_actions = NULL;
    if (stone->stone_attrs != NULL) {
	INT_CMfree_attr_list(cm, stone->stone_attrs);
	stone->stone_attrs = NULL;
    }
}

EVaction
add_proto_action(CManager cm, stone_type stone, proto_action **act)
{
    int proto_action_num = stone->proto_action_count;
    stone->proto_actions = realloc(stone->proto_actions, 
				   (proto_action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[proto_action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    ++stone->proto_action_count;
    *act = &stone->proto_actions[proto_action_num];
    return proto_action_num;
}

static void
clear_response_cache(stone_type stone)
{
    stone->response_cache_count = 0;
    /* GSE  free response entitites */
    if (stone->response_cache) free(stone->response_cache);
    stone->response_cache = NULL;
}

EVstone
INT_EVcreate_terminal_action(CManager cm, FMStructDescList format_list, 
			     EVSimpleHandlerFunc handler, void *client_data)
{
    EVstone stone = INT_EValloc_stone(cm);
    (void) INT_EVassoc_terminal_action(cm, stone, format_list, 
				       handler, client_data);
    return stone;
}    

EVaction
INT_EVassoc_terminal_action(CManager cm, EVstone stone_num, 
			    FMStructDescList format_list, EVSimpleHandlerFunc handler,
			    void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone;
    int proto_action_num;

    stone = stone_struct(evp, stone_num);
    proto_action_num = stone->proto_action_count;
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
    stone->proto_actions[proto_action_num].matching_reference_formats = NULL;
    action_num = stone->response_cache_count;
    stone->response_cache = realloc(stone->response_cache, (action_num + 1) * 
				   sizeof(stone->response_cache[0]));
    memset(&stone->response_cache[action_num], 0, sizeof(stone->response_cache[0]));
    if (format_list != NULL) {
	stone->proto_actions[proto_action_num].data_state = Requires_Decoded;
	stone->proto_actions[proto_action_num].matching_reference_formats = 
	    malloc(2*sizeof(FMFormat));
	stone->proto_actions[proto_action_num].matching_reference_formats[0] = 
	    EVregister_format_set(cm, format_list);
	stone->proto_actions[proto_action_num].matching_reference_formats[1] = NULL;
    } else {
	stone->proto_actions[proto_action_num].data_state = Requires_Contig_Encoded;
	stone->default_action = action_num;
    }
    stone->response_cache[action_num].action_type = Action_Terminal;
    stone->response_cache[action_num].requires_decoded =
	stone->proto_actions[proto_action_num].data_state;
    if (stone->proto_actions[proto_action_num].matching_reference_formats) {
	stone->response_cache[action_num].reference_format = 
	    stone->proto_actions[proto_action_num].matching_reference_formats[0];
    } else {
	stone->response_cache[action_num].reference_format = NULL;
    }
    stone->response_cache[action_num].proto_action_id = proto_action_num;
    stone->proto_action_count++;
    if (CMtrace_on(cm, EVerbose)) {
	printf("Adding Terminal action %d to stone %d\n", action_num, 
	       stone_num);
	printf("Stone dump->\n");
	dump_stone(stone);
    }
    return action_num;
}
    
EVaction
INT_EVassoc_raw_terminal_action(CManager cm, EVstone stone_num, 
				EVRawHandlerFunc handler,
				void *client_data)
{
    return INT_EVassoc_terminal_action(cm, stone_num, NULL,
				       (EVSimpleHandlerFunc)handler,
				       client_data);
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
    EVaction action_num;
    proto_action *act;
    stone_type stone;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    action_num = add_proto_action(cm, stone, &act);
    CMtrace_out(cm, EVerbose, "Adding Immediate action %d to stone %d",
		action_num, stone_num);
    stone->proto_actions = realloc(stone->proto_actions, (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].data_state = Requires_Decoded;
    stone->proto_actions[action_num].action_type = Action_Immediate;
    stone->proto_actions[action_num].o.imm.output_count = 0;
    stone->proto_actions[action_num].o.imm.output_stone_ids = malloc(sizeof(int));
    stone->proto_actions[action_num].o.imm.output_stone_ids[0] = -1;
    stone->proto_actions[action_num].o.imm.mutable_response_data = 
 	install_response_handler(cm, stone_num, action_spec, client_data, 
				 &stone->proto_actions[action_num].matching_reference_formats);
    clear_response_cache(stone);
    return action_num;
}

EVaction
INT_EVassoc_multi_action(CManager cm, EVstone stone_num, 
			  char *action_spec, void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;
    action_num = stone->proto_action_count;
    CMtrace_out(cm, EVerbose, "Adding Multi action %d to stone %d",
		action_num, stone_num);
    stone->proto_actions = realloc(stone->proto_actions, (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].data_state = Requires_Decoded;
    stone->proto_actions[action_num].action_type = Action_Multi;
    stone->proto_actions[action_num].o.imm.output_count = 0;
    stone->proto_actions[action_num].o.imm.output_stone_ids = malloc(sizeof(int));
    stone->proto_actions[action_num].o.imm.output_stone_ids[0] = -1;
    stone->proto_actions[action_num].o.imm.mutable_response_data = 
 	install_response_handler(cm, stone_num, action_spec, client_data, 
				 &stone->proto_actions[action_num].matching_reference_formats);
   stone->proto_action_count++;
    clear_response_cache(stone);
    return action_num;
}

EVaction
INT_EVassoc_congestion_action(CManager cm, EVstone stone_num, 
			      char *action_spec, void *client_data)
{
    event_path_data evp = cm->evp;
    int action_num;
    stone_type stone;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    action_num = stone->proto_action_count;
    CMtrace_out(cm, EVerbose, "Adding Congestion action %d to stone %d",
		action_num, stone_num);
    stone->proto_actions = realloc(stone->proto_actions, (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].data_state = Requires_Decoded;
    stone->proto_actions[action_num].action_type = Action_Congestion;
    stone->proto_actions[action_num].o.imm.output_count = 0;
    stone->proto_actions[action_num].o.imm.output_stone_ids = malloc(sizeof(int));
    stone->proto_actions[action_num].o.imm.output_stone_ids[0] = -1;
    stone->proto_actions[action_num].o.imm.mutable_response_data = 
 	install_response_handler(cm, stone_num, action_spec, client_data, 
				 &stone->proto_actions[action_num].matching_reference_formats);
    stone->proto_action_count++;
    clear_response_cache(stone);
    return action_num;
}

EVstone
INT_EVassoc_filter_action(CManager cm, EVstone stone_num, 
		      FMStructDescList format_list, EVSimpleHandlerFunc handler,
		      EVstone out_stone_num, void *client_data)
{
    event_path_data evp = cm->evp;
    stone_type stone;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

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
    stone->proto_actions[proto_action_num].data_state = Requires_Decoded;
    stone->proto_actions[proto_action_num].matching_reference_formats = NULL;
    if (format_list != NULL) {
	stone->proto_actions[proto_action_num].matching_reference_formats = 
	    malloc(2*sizeof(FMFormat));
	stone->proto_actions[proto_action_num].matching_reference_formats[0] = 
	    EVregister_format_set(cm, format_list);
	stone->proto_actions[proto_action_num].matching_reference_formats[1] = NULL;
    }	
    stone->proto_action_count++;
    clear_response_cache(stone);
    CMtrace_out(cm, EVerbose, "Adding filter action %d to stone %d",
		proto_action_num, stone_num);
    return proto_action_num;
}

static storage_queue_ops storage_queue_default_ops;

EVaction 
INT_EVassoc_store_action(CManager cm, EVstone stone_num, EVstone out_stone,
                            int store_limit)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    proto_action *act;
    int action_num;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    action_num = add_proto_action(cm, stone, &act);

    act->data_state = Accepts_All;
    act->action_type = Action_Store;
    act->matching_reference_formats = malloc(sizeof(FMFormat));
    act->matching_reference_formats[0] = NULL; /* signal that we accept all formats */
    storage_queue_init(cm, &act->o.store.queue, &storage_queue_default_ops, NULL);
    act->o.store.target_stone_id = out_stone;
    act->o.store.max_stored = store_limit;
    act->o.store.num_stored = 0;
    clear_response_cache(stone);
    stone->default_action = action_num;

    return action_num;
}

EVstone
INT_EVcreate_store_action(CManager cm, EVstone out_stone, int store_limit)
{
    EVstone stone = INT_EValloc_stone(cm);
    (void) INT_EVassoc_store_action(cm, stone, out_stone, store_limit);
    return stone;
}

void
INT_EVclear_stored(CManager cm, EVstone stone_num, EVaction action_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    storage_queue_ptr queue;

    stone = stone_struct(evp, stone_num);
    if (!stone) return;

    queue = &stone->proto_actions[action_num].o.store.queue;

    storage_queue_empty(cm, queue);
}

static int evpath_locked(){return 1;}

typedef struct action_tracking_state {
    int last_active_stone;
    int events_in_play;
} *action_state;

static void
raw_enqueue_event(CManager cm, queue_ptr queue, int action_id, event_item *event)
{
    queue_item *item;
    event_path_data evp = cm->evp;
    if (evp->queue_items_free_list == NULL) {
	item = malloc(sizeof(*item));
    } else {
	item = evp->queue_items_free_list;
	evp->queue_items_free_list = item->next;
    }
    item->item = event;
    item->action_id = action_id;
    reference_event(event);
    if (queue->queue_head == NULL) {
	queue->queue_head = item;
	queue->queue_tail = item;
	item->next = NULL;
    } else {
	queue->queue_tail->next = item;
	queue->queue_tail = item;
	item->next = NULL;
    }
}

static void backpressure_check(CManager, EVstone);

static void
enqueue_event(CManager cm, int stone_id, int action_id, event_item *event)
{
    event_path_data evp = cm->evp;
    stone_type stone = stone_struct(evp, stone_id);
    action_state as = evp->as;
    if (as == NULL) {
	as = evp->as = malloc(sizeof(*as));
	memset(as, 0, sizeof(*as));
    }
    raw_enqueue_event(cm, stone->queue, action_id, event);
    stone->new_enqueue_flag = 1;
    stone->queue_size++;
    backpressure_check(cm, stone_id);
    as->last_active_stone = stone_id;
    as->events_in_play++;
}

static event_item *
raw_dequeue_event(CManager cm, queue_ptr q, int *act_p)
{
    event_path_data evp = cm->evp;
    queue_item *item = q->queue_head;
    event_item *event = NULL;
    if (item == NULL) return event;
    event = item->item;
    if (q->queue_head == q->queue_tail) {
	q->queue_head = NULL;
	q->queue_tail = NULL;
    } else {
	q->queue_head = q->queue_head->next;
    }   
    if (act_p)
        *act_p = item->action_id;
    item->next = evp->queue_items_free_list;
    evp->queue_items_free_list = item;
    return event;
}

static event_item *
dequeue_event(CManager cm, stone_type stone, int *act_p)
{
    queue_ptr q = stone->queue;
    event_path_data evp = cm->evp;
    action_state as = evp->as;
    event_item *event = NULL;
    event = raw_dequeue_event(cm, q, act_p);
    stone->queue_size--;
    as->events_in_play--;
    return event;
}

static event_item *
dequeue_item(CManager cm, stone_type stone, queue_item *to_dequeue)
{
    queue_ptr q = stone->queue;
    event_path_data evp = cm->evp;
    action_state as = evp->as;
    queue_item *item = q->queue_head;
    event_item *event = NULL;
    if (item == NULL) return event;
    event = item->item;
    if (q->queue_head == to_dequeue) {
	if (q->queue_head == q->queue_tail) {
	    q->queue_head = NULL;
	    q->queue_tail = NULL;
	} else {
	    q->queue_head = q->queue_head->next;
	}
    } else {
	queue_item *cur, *last;
	last = q->queue_head;
	cur = q->queue_head->next;
	while (cur != to_dequeue) {
	    last = cur;
	    cur = cur->next;
	}
        assert(cur == to_dequeue);
	item = cur;
	last->next = cur->next;
	if (cur == q->queue_tail) {
	    q->queue_tail = last;
	}
    }
    item->next = evp->queue_items_free_list;
    evp->queue_items_free_list = item;
    stone->queue_size--;
    as->events_in_play--;
    return event;
}

void
EVdiscard_queue_item(CManager cm, int s, queue_item *item) {
    stone_type stone = stone_struct(cm->evp, s);
    (void) dequeue_item(cm, stone, item);
}

static void encode_event(CManager, event_item*);

/* {{{ storage_queue_default_* */
static void ensure_ev_owned(CManager cm, event_item *event) {
    if (event->contents == Event_App_Owned && !event->free_func) {
        encode_event(cm, event);
        event->decoded_event = NULL;
        event->event_encoded = 1;
        event->contents = Event_CM_Owned;
        assert(event->encoded_event);
    }
}

static void
storage_queue_default_empty(CManager cm, storage_queue_ptr queue) {
    empty_queue(&queue->u.queue); 
}

static void
storage_queue_default_enqueue(CManager cm, storage_queue_ptr queue, event_item *item) {
    ensure_ev_owned(cm, item);
    raw_enqueue_event(cm, &queue->u.queue, -1, item);
}

static event_item *
storage_queue_default_dequeue(CManager cm, storage_queue_ptr queue) {
    event_item *ev = raw_dequeue_event(cm, &queue->u.queue, NULL);
    return ev;
}

static storage_queue_ops storage_queue_default_ops = {
    /* init    */ NULL,
    /* cleanup */ storage_queue_default_empty,
    /* enqueue */ storage_queue_default_enqueue,
    /* dequeue */ storage_queue_default_dequeue,
    /* empty   */ storage_queue_default_empty
};
/* }}} */


extern void
INT_EVassoc_conversion_action(cm, stone_id, stage, target_format, incoming_format)
CManager cm;
int stone_id;
int stage;
FMFormat target_format;
FMFormat incoming_format;
{
    response_cache_element *act;
    stone_type stone;
    int a;
    int id_len;
    FFSTypeHandle format;
    char *server_id;

    stone = stone_struct(cm->evp, stone_id);
    if (!stone) return;

    a = stone->response_cache_count;
    server_id = get_server_ID_FMformat(incoming_format, &id_len);
    CMtrace_out(cm, EVerbose, "Adding Conversion action %d to stone %d",
		a, stone_id);
    if (CMtrace_on(cm, EVerbose)) {
	char *target_tmp = global_name_of_FMFormat(target_format);
	char *incoming_tmp = global_name_of_FMFormat(incoming_format);
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
    act->stage = (action_class) stage;

    act->o.decode.context = create_FFSContext_FM(cm->evp->fmc);
    format = FFSTypeHandle_from_encode(act->o.decode.context, 
				      server_id);
    act->o.decode.decode_format = format;
    act->o.decode.target_reference_format = target_format;
    establish_conversion(act->o.decode.context, format,
			 format_list_of_FMFormat(act->o.decode.target_reference_format));
    stone->response_cache_count++;
}

int
INT_EVaction_set_output(CManager cm, EVstone stone_num, EVaction act_num, 
		    int output_index, EVstone output_stone)
{
    stone_type stone;
    int output_count = 0;

    stone = stone_struct(cm->evp, stone_num);
    if (!stone) return -1;
    if (act_num > stone->proto_action_count) return 0;
    if (stone->proto_actions[act_num].action_type == Action_Store) {
        assert(output_index == 0); /* more than one storage output not supported */
        stone->proto_actions[act_num].o.store.target_stone_id = output_stone;
    } else {
        assert((stone->proto_actions[act_num].action_type == Action_Immediate) ||
               (stone->proto_actions[act_num].action_type == Action_Multi));
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
    }
    return 1;
}

static int
compatible_stages(int real_stage, int cache_stage) {
    return real_stage == cache_stage || (real_stage == Immediate_and_Multi && cache_stage == Immediate);
}

static action_class cached_stage_for_action(proto_action*);

static int
determine_action(CManager cm, stone_type stone, action_class stage, event_item *event, int recursed_already)
{
    int i;
    int return_response;
    if (event->reference_format == NULL) {
	CMtrace_out(cm, EVerbose, "Call to determine_action, event reference_format is NULL");
    } else {
	CMtrace_out(cm, EVerbose, "Call to determine_action, event reference_format is %p (%s)",
	   event->reference_format, name_of_FMformat(event->reference_format));
    }
    for (i=0; i < stone->response_cache_count; i++) {
        if (!compatible_stages(stage, stone->response_cache[i].stage)) {
            continue;
        }
        if ((stage != stone->response_cache[i].stage) && 
	    (stone->response_cache[i].action_type == Action_NoAction)) {
            continue;
        }
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
	    return i;
	} else if (stone->response_cache[i].reference_format == NULL &&
                   !stone->response_cache[i].requires_decoded) {
            return i;
        }
    }
    if (!recursed_already && (response_determination(cm, stone, stage, event) == 1)) {
	return determine_action(cm, stone, stage, event, 1);
    }
    /* 
     * there was no action for this event, install a dummy so we 
     * don't search again.
     */
    if (stone->response_cache_count == 0) {
	if (stone->response_cache != NULL) free(stone->response_cache);
	stone->response_cache = malloc(sizeof(stone->response_cache[0]));
    } else {
	stone->response_cache = 
	    realloc(stone->response_cache,
		    (stone->response_cache_count + 1) * sizeof(stone->response_cache[0]));
    }
    stone->response_cache[stone->response_cache_count].reference_format =
	event->reference_format;
    return_response = stone->response_cache_count++;

    if (stone->default_action != -1 
            && compatible_stages(stage, cached_stage_for_action(&stone->proto_actions[stone->default_action]))
        ) {
/*	    printf(" Returning ");
	    dump_action(stone, NULL, stone->default_action, "   ");*/
	response_cache_element *resp = 
	    &stone->response_cache[return_response];
	proto_action *proto = &stone->proto_actions[stone->default_action];
	resp->proto_action_id = stone->default_action;
	resp->action_type = proto->action_type;
	resp->requires_decoded = proto->data_state;
        resp->stage = stage;
	return return_response;
    }
    /* This should be detected elsewhere now
    if (CMtrace_on(cm, EVWarning)) {
	printf("Warning!  No action found for incoming event on stone %d\n",
	       stone->local_id);
	dump_stone(stone);
    }
    */
    stone->response_cache[return_response].action_type = Action_NoAction;
    stone->response_cache[return_response].stage = stage;
    stone->response_cache[return_response].requires_decoded = 0;

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
	    free_FFSBuffer(event->ioBuffer);
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
	    if (!FFSdecode_in_place(act->o.decode.context,
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
	    int decoded_length = FFS_est_decode_length(act->o.decode.context, 
						       event->encoded_event,
						       event->event_len);
	    CMbuffer cm_decode_buf = cm_get_data_buf(cm, decoded_length);
	    void *decode_buffer = cm_decode_buf->buffer;
	    if (event->event_len == -1) printf("BAD LENGTH\n");
	    FFSdecode_to_buffer(act->o.decode.context, event->encoded_event, 
				decode_buffer);
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
	int decoded_length = FFS_est_decode_length(act->o.decode.context, 
						   event->encoded_event,
						   event->event_len);
	event_item *new_event = get_free_event(evp);
	CMbuffer cm_decode_buf = cm_get_data_buf(cm, decoded_length);
	void *decode_buffer = cm_decode_buf->buffer;
	if (event->event_len == -1) printf("BAD LENGTH\n");
	FFSdecode_to_buffer(act->o.decode.context, 
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
    if (event->event_encoded) {
	assert(0);
    }
	
    if (event->ioBuffer != NULL) return;  /* already encoded */
    event->ioBuffer = create_FFSBuffer();
    event->encoded_event = 
	FFSencode(event->ioBuffer, event->reference_format,
		  event->decoded_event,
		  &event->event_len);
    event->event_encoded = 1;
}

extern void
cod_encode_event(CManager cm, event_item *event)
{
    encode_event(cm, event);
}

static action_class
cached_stage_for_action(proto_action *act) {
    switch (act->action_type) {
    case Action_Congestion:
        return Congestion;
    case Action_Multi:
        return Immediate_and_Multi;
    case Action_Output:
        return Output;
    case Action_Terminal:
    case Action_Filter:
    case Action_Split:
    case Action_Immediate:
    case Action_Store:
        return Immediate;
    default:
        assert(0);    
    }
}

extern event_item * 
cod_decode_event(CManager cm, int stone_num, int act_num, event_item *event) {
    stone_type stone;
    action_class stage;
    int resp_id;

    assert(!event->decoded_event);

    stone = stone_struct(cm->evp, stone_num);
    stage = cached_stage_for_action(&stone->proto_actions[act_num]);

    resp_id = determine_action(cm, stone, stage, event, 0);
    assert(stone->response_cache[resp_id].action_type == Action_Decode);
    return decode_action(cm, event, &stone->response_cache[resp_id]);
}

static void
dump_proto_action(stone_type stone, int a, const char *indent)
{
    proto_action *proto = &stone->proto_actions[a];
    printf(" Proto-Action %d - %s\n", a, action_str[proto->action_type]);
}

static void
dump_action(stone_type stone, response_cache_element *resp, int a, const char *indent)
{
    proto_action *act;
    if ((resp != NULL) && (resp->action_type == Action_NoAction)) {
	printf("NO ACTION REGISTERED\n");
	return;
    }
    act = &stone->proto_actions[a];
    printf(" Action %d - %s  ", a, action_str[act->action_type]);
    if (act->data_state == Accepts_All) {
	printf("accepts any encode state\n");
    } else if (act->data_state == Requires_Decoded) {
	printf("requires encoded\n");
    } else if (act->data_state == Requires_Contig_Encoded) {
	printf("requires contiguous encoded\n");
    } else if (act->data_state == Requires_Vector_Encoded) {
	printf("requires vector encoded\n");
    }
    printf("  expects formats ");
    if (act->matching_reference_formats) {
	int i = 0;
	while (act->matching_reference_formats[i] != NULL) {
	    char *tmp;
	    printf("\"%s\" ", tmp = global_name_of_FMFormat(act->matching_reference_formats[i]));
	    i++;
	    free(tmp);
	}
    } else {
	printf(" NULL");
    }
    printf("\n");
    switch(act->action_type) {
    case Action_Output:
	printf("  Target: %s: connection %lx, remote_stone_id %d\n",
	       (act->o.out.remote_path ? act->o.out.remote_path : "NULL" ),
	       (long)(void*)act->o.out.conn, act->o.out.remote_stone_id);
	if (act->o.out.conn != NULL) dump_attr_list(act->o.out.conn->attrs);
	if (act->o.out.conn_failed) printf("Connection has FAILED!\n");
	break;
    case Action_Terminal:
	break;
    case Action_Filter:
/*	printf("  Filter proto action number %d\n",
	act->proto_action_id);*/
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
	printf("       Target Stones:");
	{
	    int i;
	    for(i = 0; i < act->o.imm.output_count; i++) {
		if (i != act->o.imm.output_count - 1) {
		    printf(" %d,", act->o.imm.output_stone_ids[i]);
		} else {
		    printf(" %d\n", act->o.imm.output_stone_ids[i]);
		}
	    }
	}
	dump_mrd(act->o.imm.mutable_response_data);
	break;
    case Action_Store:
        printf("   Store action: %d/%d items\n", act->o.store.num_stored, 
                    act->o.store.max_stored);
    case Action_NoAction:
	printf("   NoAction\n");
	break;
    case Action_Multi:
	printf("   Multi action\n");
	printf("       Target Stones: ");
	{
	    int i;
	    for(i = 0; i < act->o.imm.output_count; i++) {
		if (i != act->o.imm.output_count - 1) {
		    printf(" %d,", act->o.imm.output_stone_ids[i]);
		} else {
		    printf(" %d", act->o.imm.output_stone_ids[i]);
		}
	    }
            printf("\n");
	}
	dump_mrd(act->o.imm.mutable_response_data);
	break;
    default:
	assert(FALSE);
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
	dump_action(stone, NULL, i, "    ");
    }
}

void
EVdump_stone(CManager cm,  EVstone stone_num)
{
    stone_type stone = stone_struct(cm->evp, stone_num);
    dump_stone(stone);
}

int
internal_path_submit(CManager cm, int local_path_id, event_item *event)
{
    event_item *event_to_submit = event;

    assert(evpath_locked());
    assert(CManager_locked(cm));
/*    stone =     stone = stone_struct(cm->evp, stone_num);
    resp_id = determine_action(cm, stone, event, 0);
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
	resp_id = determine_action(cm, stone, event_to_submit, 0);
	resp = &stone->response_cache[resp_id];
    }
    if (CMtrace_on(cm, EVerbose)) {
	printf("Enqueueing event %lx on stone %d, action %lx\n",
	       (long)event_to_submit, local_path_id, (long)resp);
	
	dump_action(stone, resp, resp->proto_action_id, "    ");
	}*/
    enqueue_event(cm, local_path_id, -1, event_to_submit);
/*    if (event != event_to_submit) {

	 * if an event was created by the decode, decrement it's
	 * ref count now.  The parameter 'event' will be dereferenced
	 * by our caller.

	return_event(evp, event_to_submit);
    }*/
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
    static atom_t CM_EVENT_SIZE = -1;
    static atom_t EV_EVENT_LSUM = -1;

    if (CM_EVENT_SIZE == -1) {
	CM_EVENT_SIZE = attr_atom_from_string("CM_EVENT_SIZE");
	EV_EVENT_LSUM = attr_atom_from_string("EV_EVENT_LSUM");
    }
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

static int
is_immediate_action(response_cache_element *act)
{
    switch(act->action_type) {
    case Action_Terminal:
    case Action_Filter:
    case Action_Split:
    case Action_Immediate:
    case Action_Store:
	return 1;
    default:
	return 0;
    }
}

static int
is_multi_action(response_cache_element *act)
{
    switch(act->action_type) {
    case Action_Multi:
	return 1;
    default:
	return 0;
    }
}

static int
is_congestion_action(response_cache_element *act) {
    return act->action_type == Action_Congestion;
}

static int do_bridge_action(CManager cm, int s);

static void backpressure_set(CManager, EVstone, int stalledp);
static int process_stone_pending_output(CManager, EVstone);

static int
process_events_stone(CManager cm, int s, action_class c)
{

/*  Process all possible events on this stone that match the class */

    event_path_data evp = cm->evp;
    stone_type stone = NULL;
    int more_pending = 0;
    queue_item *item;
    action_state as = evp->as;

    CMtrace_out(cm, EVerbose, "Considering events on stone %d\n", s);
    if (s == -1) return 0;
    if (as->last_active_stone == s) as->last_active_stone = -1;
    stone = stone_struct(cm->evp, s);
    if (!stone) return 0;
    if (stone->local_id == -1) return 0;
    if (stone->is_draining == 1) return 0;
    if (stone->is_frozen == 1) return 0;
    if (c == Immediate_and_Multi && stone->pending_output) {
        more_pending += process_stone_pending_output(cm, s);
    }
    if (is_output_stone(cm, s) && (c != Output) && (c != Congestion)) return 0;
    stone->is_processing = 1;
    
    CMtrace_out(cm, EVerbose, "Process events stone %d\n", s);
    item = stone->queue->queue_head;
    if (is_output_stone(cm, s) && (c == Output)) {
	do_bridge_action(cm, s);
	return 0;
    }
    while (item != NULL && stone->is_draining == 0 && stone->is_frozen == 0) {
	queue_item *next = item->next;
	response_cache_element *resp;
	response_cache_element *act = NULL;
        if (c != Congestion && item->action_id != -1) {
             act = &stone->response_cache[item->action_id];
        } else {
	    /* determine what kind of action to take here */
	    int resp_id;
	    event_item *event = item->item;
	    resp_id = determine_action(cm, stone, c, item->item, 0);
            assert(resp_id < stone->response_cache_count);
	    if (stone->response_cache[resp_id].action_type == Action_NoAction
                && c == Immediate_and_Multi) {
                /* ignore event */
                char *tmp = NULL;
                if (event->reference_format)
                    tmp = global_name_of_FMFormat(event->reference_format);
                printf("No action found for event %lx submitted to stone %d\n",
                       (long)event, s);
                dump_stone(stone_struct(evp, s));
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
                event = dequeue_item(cm, stone, item);
                return_event(evp, event);
            }
	    resp = &stone->response_cache[resp_id];
	    if (resp->action_type == Action_Decode) {
		event_item *event_to_submit;
		CMtrace_out(cm, EVerbose, "Decoding event, action id %d", resp_id);
		event_to_submit = decode_action(cm, event, resp);
		if (event_to_submit == NULL) return more_pending;
		item->item = event_to_submit;
		resp_id = determine_action(cm, stone, c, event_to_submit, 0);
		resp = &stone->response_cache[resp_id];
	    }
	    if (CMtrace_on(cm, EVerbose)) {
		printf("next action event %lx on stone %d, action %lx\n",
		       (long)event, s, (long)resp);
		
		dump_action(stone, resp, resp->proto_action_id, "    ");
	    }
            act = &stone->response_cache[resp_id];
            if (c == Immediate_and_Multi && act->action_type != Action_NoAction) {
                item->action_id = resp_id;
            }
	}
        assert(item->action_id < stone->response_cache_count);
        backpressure_check(cm, s);
        if (!compatible_stages(c, act->stage)) {
            /* do nothing */
        } else if (is_immediate_action(act)) {
	    event_item *event = dequeue_item(cm, stone, item);
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
		if ((p->data_state == Requires_Contig_Encoded) && 
		    (event->event_encoded == 0)) {
		    encode_event(cm, event);
		}
		if (event->event_encoded == 0) {
		    out = (handler)(cm, event->decoded_event, client_data,
				    event->attrs);
		} else {
		    EVRawHandlerFunc handler = (EVRawHandlerFunc)term->handler;
		    out = (handler)(cm, event->encoded_event, event->event_len,
				    client_data, event->attrs);
		}
		CManager_lock(cm);
		cm->evp->current_event_item = NULL;
		if (act->action_type == Action_Filter) {
		    if (out) {
			CMtrace_out(cm, EVerbose, "Filter passed event to stone %d, submitting", term->target_stone_id);
			internal_path_submit(cm, 
					     term->target_stone_id,
					     event);
			more_pending++;
		    } else {
			CMtrace_out(cm, EVerbose, "Filter discarded event");
		    }			    
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
		int in_play = as->events_in_play;
		proto_action *p = &stone->proto_actions[act->proto_action_id];
		/* data is already in the right format */
		func = act->o.imm.handler;
		client_data = act->o.imm.client_data;
		out_stones = p->o.imm.output_stone_ids;
		out_count = p->o.imm.output_count;
		func(cm, event, client_data, event->attrs, out_count, out_stones);
		return_event(evp, event);
		if (as->events_in_play > in_play)
		    more_pending++;   /* maybe??? */
		break;
	    }
            case Action_Store: {
		proto_action *p = &stone->proto_actions[act->proto_action_id];
                storage_queue_enqueue(cm, &p->o.store.queue, event);
                CMtrace_out(cm, EVerbose, "Enqueued item to store");
                if (++p->o.store.num_stored > p->o.store.max_stored
                        && p->o.store.max_stored != -1) {
                    CMtrace_out(cm, EVerbose, "Dequeuing item because of limit");
                    event_item *last_event;
                    last_event = storage_queue_dequeue(cm, &p->o.store.queue);
                    assert(last_event->ref_count > 0);
                    --p->o.store.num_stored;
                    internal_path_submit(cm, p->o.store.target_stone_id, last_event);
                    return_event(evp, last_event);
                    more_pending++;
                }
                break;
            }
	    case Action_Output:
	    default:
		assert(FALSE);
	    }
	} else if (is_multi_action(act) || is_congestion_action(act)) {
            proto_action *p = &stone->proto_actions[act->proto_action_id];
	    /* event_item *event = dequeue_item(cm, stone->queue, item); XXX */
            if ((act->o.multi.handler)(cm, stone->queue, item,
					act->o.multi.client_data, p->o.imm.output_count,
					p->o.imm.output_stone_ids))
                more_pending++;
            break;    
	} 
	item = next;
    }
    return more_pending;
}
	

static
int
process_local_actions(cm)
CManager cm;
{
    event_path_data evp = cm->evp;
    action_state as = evp->as;
    int s, more_pending = 0;
    CMtrace_out(cm, EVerbose, "Process local actions");
    if (as == NULL) {
	as = evp->as = malloc(sizeof(*as));
	memset(as, 0, sizeof(*as));
    }
 restart:
    if (as->last_active_stone != -1) {
	more_pending = 1;
	while (more_pending) {
	    CMtrace_out(cm, EVerbose, "Process local actions on stone %d",
			as->last_active_stone);
	    
	    CMtrace_out(cm, EVerbose, "0 - in-play %d", as->events_in_play);
	    more_pending = process_events_stone(cm, as->last_active_stone, Immediate);
	}
    }
    if (as->events_in_play > 0) {
	/* check all stones */
	for (s = evp->stone_base_num; s < evp->stone_count + evp->stone_base_num; s++) {
	    stone_type stone = stone_struct(evp, s);
	    if (stone->local_id == -1) continue;
	    if (stone->is_draining == 1) continue;
	    if (stone->is_frozen == 1) continue;
	    CMtrace_out(cm, EVerbose, "1 - in-play %d", as->events_in_play);
	    more_pending += process_events_stone(cm, s, Immediate_and_Multi);
	    if (more_pending && (as->last_active_stone != -1)) goto restart;
	}
    }
    if (as->last_active_stone != -1) {
	CMtrace_out(cm, EVerbose, "Process output actions on stone %d",
		    as->last_active_stone);
	CMtrace_out(cm, EVerbose, "2 - in-play %d", as->events_in_play);
	more_pending += process_events_stone(cm, as->last_active_stone, Output);
    }
    if (as->events_in_play > 0) {
	/* check all stones */
	for (s = evp->stone_base_num; s < evp->stone_count + evp->stone_base_num; s++) {
	    stone_type stone = stone_struct(evp, s);
	    if (stone->local_id == -1) continue;
	    if (stone->is_frozen == 1) continue;
	    CMtrace_out(cm, EVerbose, "3 - in-play %d", as->events_in_play);
	    more_pending += process_events_stone(cm, s, Output);
	}
    }

    return more_pending;
}

void
INT_EVforget_connection(CManager cm, CMConnection conn)
{
    event_path_data evp = cm->evp;
    int s;
    for (s = evp->stone_base_num; s < evp->stone_count + evp->stone_base_num; ++s)  {
	stone_type stone = stone_struct(evp, s);
        if (stone->last_remote_source == conn) {
            stone->last_remote_source = NULL;
            stone->squelch_depth = 0;
        }
    }
}


static void
stone_close_handler(CManager cm, CMConnection conn, void *client_data)
{
    event_path_data evp = cm->evp;
    int s = (long)client_data;  /* stone ID */
    int a = 0;
    stone_type stone = stone_struct(evp, s);
    CMtrace_out(cm, EVerbose, "Got a close for connection %p on stone %d, shutting down",
		conn, s);
    for (a=0 ; a < stone->proto_action_count; a++) {
	proto_action *act = &stone->proto_actions[a];
	if ((act->action_type == Action_Output) && 
	    (act->o.out.conn == conn)) {
	    act->o.out.conn_failed = 1;
	    act->o.out.conn = NULL;
	    INT_CMConnection_close(conn);   /* dereference the connection */
/*	    while (act->queue->queue_head != NULL) {
		int action_id;
		event_item *event = dequeue_event(cm, act->queue, &action_id);
		return_event(evp, event);
		}*/
	}
    }
}

static void
write_callback_handler(CManager cm, CMConnection conn, void *client_data)
{
    event_path_data evp = cm->evp;
    int s = (int)(long) client_data;
    stone_type stone = stone_struct(evp, s);
    CMtrace_out(cm, EVerbose, "In Write callback, write_pending is %d, stone is %d\n", conn->write_pending, s);
    if (conn->write_pending) {
	/* nothing for EVPath level to do yet */
	return;
    }
    assert(CManager_locked(conn->cm));
    assert(stone->write_callback != -1);
    INT_CMunregister_write_callback(conn, stone->write_callback);
    stone->write_callback = -1;
    /* try calling the congestion handler before we write again... */
    process_events_stone(cm, s, Congestion);
    do_bridge_action(cm, s);
}

static
int
do_bridge_action(CManager cm, int s)
{
    event_path_data evp = cm->evp;
    proto_action *act = NULL;
    stone_type stone;
    int a;
    CMtrace_out(cm, EVerbose, "Process output action on stone %d", s);
    stone = stone_struct(evp, s);

    if (stone->is_frozen || stone->is_draining) return 0;
    stone->is_outputting = 1;
    for (a=0 ; a < stone->proto_action_count && stone->is_frozen == 0 && stone->is_draining == 0; a++) {
	if (stone->proto_actions[a].action_type == Action_Output) {
	    act = &stone->proto_actions[a];
	}
    }
    while (stone->queue->queue_head != NULL) {
	int action_id, ret = 1;
	if (act->o.out.conn && 
	    INT_CMConnection_write_would_block(act->o.out.conn)) {
            queue_item *q = stone->queue->queue_head;
	    int i = 0;
	    CMtrace_out(cm, EVerbose, "Would call congestion_handler, new flag %d\n", stone->new_enqueue_flag);
/*	    if (stone->new_enqueue_flag == 1) {*/
		stone->new_enqueue_flag = 0;
                while (q != NULL) {q = q->next; i++;}
		CMtrace_out(cm, EVerbose, "Would call congestion_handler, %d items queued\n", i);
		if (stone->write_callback == -1) {
		    stone->write_callback = 
                        INT_CMregister_write_callback(act->o.out.conn, 
						  write_callback_handler, 
						  (void*)(long)s);
		}
                process_events_stone(cm, s, Congestion);
		return 0;
/*	    }*/
	}
	event_item *event = dequeue_event(cm, stone, &action_id);
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
		    tmp_format.fmformat = event->reference_format;
		    tmp_format.format_name = name_of_FMformat(event->reference_format);
		    tmp_format.registration_pending = 0;
		    ret = internal_write_event(act->o.out.conn, &tmp_format,
					       &act->o.out.remote_stone_id, 4, 
					       event, event->attrs);
		}
	    }
	}
	return_event(evp, event);
        backpressure_check(cm, s);
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
    stone->is_outputting = 0;
    return 0;
}

void
INT_EVset_store_limit(CManager cm, EVstone stone_num, EVaction action_num, int new_limit)
{
    int old_limit;
    event_path_data evp = cm->evp;
    stone_type stone;
    proto_action *p;

    stone = stone_struct(evp, stone_num);
    if (!stone) return;

    p = &stone->proto_actions[action_num];
    old_limit = p->o.store.max_stored;
    p->o.store.max_stored = new_limit;
    if (p->o.store.max_stored != -1) {
        while (p->o.store.num_stored > p->o.store.max_stored) {
            event_item *item;
            item = storage_queue_dequeue(cm, &p->o.store.queue);
            if (!item)
                break;
            p->o.store.num_stored--;
            internal_path_submit(cm, p->o.store.target_stone_id, item);
            while (process_local_actions(cm));
            return_event(evp, item);
        }
    }
}

extern EVaction
INT_EVassoc_mutated_imm_action(CManager cm, EVstone stone_id, EVaction act_num,
			       EVImmediateHandlerFunc func, void *client_data, 
			       FMFormat reference_format)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int resp_num;

    stone = stone_struct(evp, stone_id);
    if (!stone) return -1;

    resp_num = stone->response_cache_count;
    stone->response_cache = realloc(stone->response_cache, sizeof(stone->response_cache[0]) * (resp_num + 1));
    response_cache_element *resp = &stone->response_cache[stone->response_cache_count];
    resp->action_type = Action_Immediate;
    resp->requires_decoded = 1;
    resp->proto_action_id = act_num;
    resp->o.imm.handler = func;
    resp->o.imm.client_data = client_data;
    resp->reference_format = reference_format;
    resp->stage = cached_stage_for_action(&stone->proto_actions[act_num]);
    stone->response_cache_count++;
    return resp_num;
}

extern EVaction
INT_EVassoc_mutated_multi_action(CManager cm, EVstone stone_id, EVaction act_num,
				  EVMultiHandlerFunc func, void *client_data, 
				  FMFormat *reference_formats)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int resp_num;
    int queue_count = 0, i;

    stone = stone_struct(evp, stone_id);
    resp_num = stone->response_cache_count;
    while (reference_formats[queue_count] != NULL) queue_count++;
    stone->response_cache = realloc(stone->response_cache, sizeof(stone->response_cache[0]) * (resp_num + queue_count));
    response_cache_element *resp;
    for (i=0; i < queue_count; i++) {
	resp = &stone->response_cache[stone->response_cache_count + i];
	resp->action_type = stone->proto_actions[act_num].action_type;
	resp->requires_decoded = 1;
	resp->proto_action_id = act_num;
	resp->o.multi.handler = func;
	resp->o.multi.client_data = client_data;
        resp->stage = cached_stage_for_action(&stone->proto_actions[act_num]);
	resp->reference_format = reference_formats[i];
    }
    stone->response_cache_count += queue_count;
    return resp_num;
}

extern EVstone
EVcreate_output_action(CManager cm, attr_list contact_list,
			   EVstone remote_stone)
{
    static int first = 1;
    if (first) {
	first = 0;
	printf("EVassoc_output_action is deprecated.  Please use EVassoc_bridge_action()\n");
    }
    return EVcreate_bridge_action(cm, contact_list, remote_stone);
}

extern EVstone
INT_EVcreate_bridge_action(CManager cm, attr_list contact_list,
			   EVstone remote_stone)
{
    EVstone stone = INT_EValloc_stone(cm);
    INT_EVassoc_bridge_action(cm, stone, contact_list, remote_stone);
    return stone;
}

static int
is_output_stone(CManager cm, EVstone stone_num)
{
    event_path_data evp = cm->evp;
    stone_type stone = stone_struct(evp, stone_num);
    if (stone->default_action == -1) return 0;
    return (stone->proto_actions[stone->default_action].action_type == Action_Output);
}

extern EVaction
EVassoc_output_action(CManager cm, EVstone stone_num, attr_list contact_list,
		      EVstone remote_stone)
{
    static int first = 1;
    if (first) {
	first = 0;
	printf("EVassoc_output_action is deprecated.  Please use EVassoc_bridge_action()\n");
    }
    return EVassoc_bridge_action(cm, stone_num, contact_list, remote_stone);
}

extern EVaction
INT_EVassoc_bridge_action(CManager cm, EVstone stone_num, attr_list contact_list,
		      EVstone remote_stone)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    int action_num;
    CMConnection conn;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    action_num = stone->proto_action_count;
    conn = INT_CMget_conn(cm, contact_list);
    if (conn == NULL) {
	if (CMtrace_on(cm, EVWarning)) {
	    printf("EVassoc_bridge_action - failed to contact host at contact point \n\t");
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
    stone_type stone;
    int action_num;
    int target_count = 0, i;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    action_num = stone->proto_action_count;
    stone->proto_actions = realloc(stone->proto_actions, 
				   (action_num + 1) * 
				   sizeof(stone->proto_actions[0]));
    memset(&stone->proto_actions[action_num], 0, 
	   sizeof(stone->proto_actions[0]));
    stone->proto_actions[action_num].action_type = Action_Split;
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
    stone_type stone;
    EVstone *target_stone_list;
    int target_count = 0;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

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
    stone_type stone;
    EVstone *target_stone_list;
    int target_count = 0;

    stone = stone_struct(evp, stone_num);
    if (!stone) return;

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

void
INT_EVsend_stored(CManager cm, EVstone stone_num, EVaction action_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    event_item *item;
    proto_action *p;

    stone = stone_struct(evp, stone_num);
    if (!stone) return;

    p = &stone->proto_actions[action_num];
    while ((item = storage_queue_dequeue(cm, &p->o.store.queue)) != NULL) {
        internal_path_submit(cm, p->o.store.target_stone_id, item);
        p->o.store.num_stored--;
        return_event(evp, item);
        while (process_local_actions(cm));
    }
}

/* this is called to make a store-send happen; it just
 * runs the standard loop later.
 */
static void
deferred_process_actions(CManager cm, void *client_data) {
    CManager_lock(cm);
    while (process_local_actions(cm));
    CManager_unlock(cm);
}

void
INT_EVstore_start_send(CManager cm, EVstone stone_num, EVaction action_num)
{
    event_path_data evp = cm->evp;
    action_state as = evp->as;
    stone_type stone;
    proto_action *act;

    stone = stone_struct(evp, stone_num);
    if (!stone) return;

    act = &stone->proto_actions[action_num];

    if (act->o.store.num_stored == 0) return;
    if (act->o.store.is_sending == 1) return;

    act->o.store.is_sending = 1;
    act->o.store.is_paused = 0;
    as->events_in_play++;
    stone->pending_output++;
    
    /* make sure the local action loop is called soon */
    (void) INT_CMadd_delayed_task(cm, 0, 0, deferred_process_actions, NULL);
}

static int 
process_stone_pending_output(CManager cm, EVstone stone_num) {
    event_path_data evp = cm->evp;
    action_state as = evp->as;
    stone_type stone = stone_struct(evp, stone_num);
    EVaction action_num;
    int found = 0;
    int more_pending = 0;

    for (action_num = 0; action_num < stone->proto_action_count
            && found < stone->pending_output; ++action_num) {
        proto_action *act = &stone->proto_actions[action_num];
        if (act->action_type == Action_Store &&
            act->o.store.is_sending && !act->o.store.is_paused) {
            event_item *item;
            ++found;
            item = storage_queue_dequeue(cm, &act->o.store.queue);
            assert(item->ref_count > 0);
            assert(!stone_struct(evp, act->o.store.target_stone_id)->is_stalled);
            internal_path_submit(cm, act->o.store.target_stone_id, item);
            /* printf("submitted one <%d / %d>\n", as->events_in_play, as->last_active_stone); */
            act->o.store.num_stored--;
            /* return_event(evp, item); */
            if (!act->o.store.num_stored) {
                /* printf("stopping sending\n"); */
                act->o.store.is_sending = act->o.store.is_paused = 0;
                as->events_in_play--;
                stone->pending_output--;
                found--;
            } else {
                ++more_pending;
            }
        }
    }
    return more_pending;
}

int
INT_EVstore_is_sending(CManager cm, EVstone stone_num, EVaction action_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;;
    proto_action *p;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    p = &stone->proto_actions[action_num];
    return p->o.store.is_sending;
}

int
INT_EVstore_count(CManager cm, EVstone stone_num, EVaction action_num)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    proto_action *p;

    stone = stone_struct(evp, stone_num);
    if (!stone) return -1;

    p = &stone->proto_actions[action_num];
    return p->o.store.num_stored;
}

struct source_info {
    EVstone to_stone;
    void *user_data;
    enum { SOURCE_ACTION, SOURCE_REMOTE } type;
    EVstone stone;
    union {
        struct {
            EVaction action;
            int would_recurse;
        } action;
        struct {
            CMConnection conn;
        } remote;
    }u;
};

typedef void (*ForeachSourceCB)(CManager, struct source_info *);

static void
foreach_source_inner(CManager cm, EVstone to_stone, char *seen,
        ForeachSourceCB cb, struct source_info *info) {
    event_path_data evp = cm->evp;
    EVstone cur_stone;
    for (cur_stone = evp->stone_base_num; cur_stone < evp->stone_count + evp->stone_base_num; ++cur_stone) {
        EVaction cur_action;
        stone_type stone = stone_struct(evp, cur_stone);
        if (seen[cur_stone - evp->stone_base_num]) continue;
        if (stone->local_id == -1) continue;
        if (cur_stone == to_stone) {
            if (stone->last_remote_source != NULL) {
                info->type = SOURCE_REMOTE;
                info->stone = cur_stone;
                info->u.remote.conn = stone->last_remote_source;
                cb(cm, info);
            }
        } else {
            int was_stalled = stone->is_stalled;
            for (cur_action = 0; cur_action < stone->proto_action_count; ++cur_action) {
                proto_action *act;
                int matches = 0;
                int matches_recursive = 0;
                act = &stone->proto_actions[cur_action];
                switch (act->action_type) {
                case Action_Store:
                    if (act->o.store.target_stone_id == to_stone) {
                        ++matches;
                    }
                    break;
                case Action_Split:
                    {
                        int i;
                        for (i = 0; act->o.split_stone_targets[i] != -1; ++i) {
                            if (act->o.split_stone_targets[i] == to_stone) {
                                ++matches;
                                ++matches_recursive;
                            }
                        }
                    }
                    break;
                case Action_Filter:
                    if (to_stone == act->o.term.target_stone_id) {
                        ++matches;
                        ++matches_recursive;
                    }
                    break;
                case Action_Immediate:
                    {
                        int i;
                        for (i = 0; i < act->o.imm.output_count; ++i) {
                            if (to_stone == act->o.imm.output_stone_ids[i]) {
                                ++matches;
                                ++matches_recursive;
                            }
                        }
                    }
                    break;
                case Action_Terminal:
                    /* nothing to do */
                    break;
                default: ;
                    /* printf("source searching: unhandled type %d\n", act->action_type); */
                    /* TODO unhandled cases
                            - Source handles?
                     */
                }
                if (matches) {
                    info->type = SOURCE_ACTION;
                    info->stone = cur_stone;
                    info->u.action.action = cur_action;
                    info->u.action.would_recurse = matches_recursive;
                    cb(cm, info);
                }
                /* If a stone is stalled, conceptually no traffic is going to it.
                 * Note that the only way we should be trying to recurse through
                 * a stalled stone is if it were manually stalled.
                 */
                if (!was_stalled && matches_recursive) {
                    /* avoid infinite recursion */
                    seen[cur_stone - evp->stone_base_num] = 1;
                    foreach_source_inner(cm, cur_stone, seen, cb, info);
                    seen[cur_stone - evp->stone_base_num] = 0; /* XXX */
                }
            }
        }
    }
}

static void
foreach_source(CManager cm, EVstone to_stone, ForeachSourceCB cb, void *user_data) {
    char* seen = calloc(1, cm->evp->stone_count); /* XXX try to keep static */
    struct source_info info;
    info.user_data = user_data;
    info.to_stone = to_stone;
    foreach_source_inner(cm, to_stone, seen, cb, &info);
    free(seen);
}

static void
backpressure_transition(CManager cm, EVstone s, stall_source src, int new_value);

enum { CONTROL_SQUELCH, CONTROL_UNSQUELCH };

static void
register_backpressure_callback(CManager cm, EVstone s, EVSubmitCallbackFunc cb, void *user_data) {
    stall_callback *new_cb = INT_CMmalloc(sizeof(stall_callback));
    stone_type stone = stone_struct(cm->evp, s);
    assert(CManager_locked(cm));
    new_cb->cb = cb;
    new_cb->user_data = user_data;
    new_cb->next = stone->unstall_callbacks;
    stone->unstall_callbacks = new_cb;
}

static void
do_backpressure_unstall_callbacks(CManager cm, EVstone stone_id) {
    stone_type stone = stone_struct(cm->evp, stone_id);
    stall_callback *cur = stone->unstall_callbacks;
    assert(CManager_locked(cm));
    if (!cur) return;
    stone->unstall_callbacks = NULL;
    CManager_unlock(cm);
    while (cur) {
        stall_callback *next = cur->next;
        (cur->cb)(cm, stone_id, cur->user_data);
        INT_CMfree(cur);
        cur = next;
    }
    CManager_lock(cm);
}


static void
backpressure_set_one(CManager cm, struct source_info *info)
{
    event_path_data evp = cm->evp;
    action_state as = evp->as;
    assert(as->events_in_play >= 0);
    int s = info->to_stone;
    stone_type to_stone = stone_struct(evp,s);
    stone_type stone = stone_struct(evp, info->stone);
    switch (info->type) {
    case SOURCE_ACTION: {
            proto_action *act = &stone->proto_actions[info->u.action.action];
            
            if (info->u.action.would_recurse) {
                /* If we might be stalling the stone that has sources, we mark it as stalled before
                 * calling * backpressure_transition() because we are already recursing to its sources
                 * and performing actions as if the stone is stalled. If the stone unstalls
                 * as a result of the transition, we do want to do the recursive search
                 * since we do not recurse through stalled stones (though we do set would_recurse
                 * for them). 
                 */
                if (to_stone->is_stalled) {
                    printf("recurse stall %d\n", info->stone);
                    stone->is_stalled = 1;
                } else {
                    printf("recurse unstall %d\n", info->stone);
                    do_backpressure_unstall_callbacks(cm, info->stone);
                }
                /* TODO for, e.g., split stones check that we should actually unstall it 
                 * (maybe just count our upstream stall depth?) 
                 */
                backpressure_transition(cm, info->stone, Stall_Upstream, to_stone->is_stalled);
            }
            switch (act->action_type) {
            case Action_Store:
                {
                    struct storage_proto_vals *store = &act->o.store;
                    if (store->is_paused != to_stone->is_stalled) {
                        store->is_paused = to_stone->is_stalled;
                        if (store->is_sending) {
                            if (store->is_paused) {
                                --as->events_in_play;
                                --stone->pending_output;
                            } else {
                                ++as->events_in_play;
                                ++stone->pending_output;
                                (void) INT_CMadd_delayed_task(cm, 0, 0, deferred_process_actions, NULL);
                            }
                        }
                    }
                }
                break;
            default:;
                /* printf("unhandled action %d\n", act->action_type); */
                /* TODO more cases? */
            }
        }
        break;
    case SOURCE_REMOTE: {
            if (to_stone->is_stalled) {
                if (stone->squelch_depth++ == 0) {
                    INT_CMwrite_evcontrol(info->u.remote.conn, CONTROL_SQUELCH, info->stone);
                }
            } else if (0 == --stone->squelch_depth) {
                INT_CMwrite_evcontrol(info->u.remote.conn, CONTROL_UNSQUELCH, info->stone);
            }
        }
        break;
    default:
        /* XXX */
        ;
    }
}

static void
backpressure_set(CManager cm, EVstone to_stone, int stalledp) {
    event_path_data evp = cm->evp;
    stone_type stone = stone_struct(evp, to_stone);
    assert(cm->evp->use_backpressure);
    if (stone->is_stalled == stalledp) {
        return;
    }
    /*
    if (stalledp) {
        printf("backpressure stalled %d\n", (int) to_stone);
    } else {
        printf("backpressure unstalled %d\n", (int) to_stone);
    }
    */
    stone->is_stalled = stalledp;
    if (!stalledp) {
        do_backpressure_unstall_callbacks(cm, to_stone);
    }
    foreach_source(cm, to_stone, backpressure_set_one, NULL);
}

static void
backpressure_transition(CManager cm, EVstone s, stall_source src, int new_value) {
    stone_type stone = stone_struct(cm->evp, s);
    assert(cm->evp->use_backpressure);
    if (new_value) {
        stone->stall_from |= src;
    } else {
        stone->stall_from &= ~src;
    }
    backpressure_set(cm, s, stone->stall_from ? 1 : 0);
}

static void
backpressure_check(CManager cm, EVstone s) {
    if (cm->evp->use_backpressure) {
        stone_type stone = stone_struct(cm->evp, s);
        int old_stalled = stone->is_stalled;
        int low_threshold = 50, high_threshold = 200;
        if (stone->stone_attrs) {
	    static atom_t EV_BACKPRESSURE_HIGH = -1;
	    static atom_t EV_BACKPRESSURE_LOW = -1;
	    if (EV_BACKPRESSURE_HIGH == -1) {
		EV_BACKPRESSURE_HIGH = attr_atom_from_string("EV_BACKPRESSURE_HIGH");
		EV_BACKPRESSURE_LOW = attr_atom_from_string("EV_BACKPRESSURE_LOW");
	    }
            get_int_attr(stone->stone_attrs, EV_BACKPRESSURE_HIGH, &high_threshold);
            get_int_attr(stone->stone_attrs, EV_BACKPRESSURE_LOW, &low_threshold);
        }
        backpressure_transition(cm, s, Stall_Overload,
            (stone->queue_size > (old_stalled ? low_threshold : high_threshold)));
    }
}

int
INT_EVsubmit_or_wait(EVsource source, void *data, attr_list attrs,
                    EVSubmitCallbackFunc cb, void *user_data) {
    CManager cm = source->cm;
    stone_type stone;

    stone = stone_struct(cm->evp, source->local_stone_id);
    if (!stone) return -1;

    if (stone->is_stalled) {
        register_backpressure_callback(source->cm, source->local_stone_id, cb, user_data);
        return 0;
    } else {
        INT_EVsubmit(source, data, attrs);
        return 1;
    }
}

int
INT_EVsubmit_encoded_or_wait(CManager cm, EVstone s, void *data, int data_len, 
                    attr_list attrs, EVSubmitCallbackFunc cb, void *user_data) {
    stone_type stone;
    stone = stone_struct(cm->evp, s);
    if (!stone) return -1;

    if (stone->is_stalled) {
        register_backpressure_callback(cm, s, cb, user_data);
        return 0;
    } else {
        INT_EVsubmit_encoded(cm, s, data, data_len, attrs);
        return 1;
    }
}

void
INT_EVstall_stone(CManager cm, EVstone s) {
    backpressure_transition(cm, s, Stall_Requested, 1);
}

void
INT_EVunstall_stone(CManager cm, EVstone s) {
    backpressure_transition(cm, s, Stall_Requested, 0);
}

void
INT_EVhandle_control_message(CManager cm, CMConnection conn, unsigned char type, int arg) {
    event_path_data evp = cm->evp;
    switch (type) {
    case CONTROL_SQUELCH:
    case CONTROL_UNSQUELCH: {
            int s;
            stone_type stone;
            for (s = evp->stone_base_num; s < evp->stone_count + evp->stone_base_num; ++s) {
                stone = stone_struct(evp, s);
                if (is_output_stone(cm, s) &&  stone->proto_actions[stone->default_action].o.out.conn == conn
                        && stone->proto_actions[stone->default_action].o.out.remote_stone_id == arg) {
                    backpressure_transition(cm, s, Stall_Squelch, type == CONTROL_SQUELCH);
                }
            }
        }
        break;
    default:
        assert(FALSE);
    }
}

extern FMFormat
EVregister_format_set(CManager cm, FMStructDescList list)
{
    FMFormat format;

    format = register_data_format(cm->evp->fmc, list);
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
    CMTaskHandle handle;
    stone_type stone;

    stone = stone_struct(cm->evp, stone_num);
    if (!stone) return;

    handle = INT_CMadd_periodic_task(cm, period_sec, period_usec,
				     EVauto_submit_func, 
				     (void*)(long)stone_num);
    stone->periodic_handle = handle;
    CMtrace_out(cm, EVerbose, "Enabling auto events on stone %d", stone_num);
}



extern EVsource
INT_EVcreate_submit_handle(CManager cm, EVstone stone, FMStructDescList data_format)
{
    EVsource source = malloc(sizeof(*source));
    memset(source, 0, sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    source->preencoded = 0;
    if (data_format != NULL) {
	source->format = INT_CMregister_format(cm, data_format);
	source->reference_format = EVregister_format_set(cm, data_format);
    };
    return source;
}

extern EVsource
INT_EVcreate_submit_handle_free(CManager cm, EVstone stone, 
			    FMStructDescList data_format, 
			    EVFreeFunction free_func, void *free_data)
{
    EVsource source = malloc(sizeof(*source));
    memset(source, 0, sizeof(*source));
    source->local_stone_id = stone;
    source->cm = cm;
    source->format = INT_CMregister_format(cm, data_format);
    source->reference_format = EVregister_format_set(cm, data_format);
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
    stone_type stone;
    event->contents = Event_CM_Owned;
    event->event_encoded = 1;
    event->event_len = length;
    event->encoded_event = buffer;
    event->reference_format = FMFormat_of_original(FFSTypeHandle_from_encode(evp->ffsc, 
							buffer));
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
	r = FMdump_encoded_data(event->reference_format,
				event->encoded_event, dump_char_limit);
	if (r && !warned) {
	    printf("\n\n  ****  Warning **** CM record dump truncated\n");
	    printf("  To change size limits, set CMDumpSize environment variable.\n\n\n");
	    warned++;
	}
    }
    INT_CMtake_buffer(cm, buffer);
    event->cm = cm;
    stone = stone_struct(evp, stone_id);
    if (stone->squelch_depth == 0) {
        stone->last_remote_source = conn;
    }
    internal_path_submit(cm, stone_id, event);
    return_event(evp, event);
    while (process_local_actions(cm));
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
    if (event->ref_count != 1 && !event->event_encoded) {
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
	event->reference_format = FMFormat_of_original(FFSTypeHandle_from_encode(evp->ffsc, 
							    data));
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
    if (event->ref_count != 1 && !event->event_encoded) {
	encode_event(source->cm, event);  /* reassign memory */
    }
    return_event(source->cm->evp, event);
}

void
INT_EVsubmit_encoded(CManager cm, EVstone stone, void *data, int data_len, attr_list attrs)
{
    event_path_data evp = cm->evp;
    event_item *event = get_free_event(evp);
    if (stone_struct(evp, stone) == NULL) return;

    event->contents = Event_App_Owned;
    event->event_encoded = 1;
    event->encoded_event = data;
    event->event_len = data_len;
    event->reference_format = FMFormat_of_original(FFSTypeHandle_from_encode(evp->ffsc, 
							data));

    event->attrs = CMadd_ref_attr_list(cm, attrs);
    internal_path_submit(cm, stone, event);
    while (process_local_actions(cm));
    return_event(cm->evp, event);
}

static void
free_evp(CManager cm, void *not_used)
{
    event_path_data evp = cm->evp;
    int s;
    CMtrace_out(cm, CMFreeVerbose, "Freeing evpath information, evp %lx", (long) evp);
    for (s = 0 ; s < evp->stone_count; s++) {
	INT_EVfree_stone(cm, s + evp->stone_base_num);
    }
    cm->evp = NULL;
    if (evp == NULL) return;
    free(evp->stone_map);
    free(evp->as);
    /* freed when CM frees it */
    /*  free_FFSContext(evp->ffsc);*/
    while (evp->queue_items_free_list != NULL) {
	queue_item *tmp = evp->queue_items_free_list->next;
	free(evp->queue_items_free_list);
	evp->queue_items_free_list = tmp;
    }
    thr_mutex_free(evp->lock);
    free(evp);
}

void
EVPinit(CManager cm)
{
    static int first_evpinit = 1;
    cm->evp = INT_CMmalloc(sizeof( struct _event_path_data));
    memset(cm->evp, 0, sizeof( struct _event_path_data));
    cm->evp->ffsc = cm->FFScontext;
    cm->evp->fmc = FMContext_from_FFS(cm->evp->ffsc);
    cm->evp->stone_base_num = 0;
    if (!first_evpinit) {
	/* 
	 * after the first evpinit, start stones at random base number,
	 * just so that we're more likely to catch mitmatched stone/CM
	 * combos in threaded situations.
	*/
	cm->evp->stone_base_num = lrand48() & 0xffff;
    }
    first_evpinit = 0;
    cm->evp->queue_items_free_list = NULL;
    cm->evp->lock = thr_mutex_alloc();
    internal_add_shutdown_task(cm, free_evp, NULL);
    {
        char *backpressure_env;
        backpressure_env = cercs_getenv("EVBackpressure");
        if (backpressure_env && atoi(backpressure_env) != 0) {
            cm->evp->use_backpressure = 1;
        } else {
            cm->evp->use_backpressure = 0;
        }
    }
    REVPinit(cm);
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
    if (!(((cur->decoded_event <= event) &&
	   ((char *) event <= ((char *) cur->decoded_event + cur->event_len))) ||
	  ((cur->encoded_event <= event) &&
	   ((char *) event <= ((char *) cur->encoded_event + cur->event_len))))) {
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
	if (((tmp->item->decoded_event <= event) &&
	    ((char *) event <= ((char *) tmp->item->decoded_event + tmp->item->event_len))) ||
	    ((tmp->item->encoded_event <= event) &&
	     ((char *) event <= ((char *) tmp->item->encoded_event + tmp->item->event_len)))) {
	    if (last == NULL) {
		evp->taken_events_list = tmp->next;
	    } else {
		last->next = tmp->next;
	    }
	    return_event(cm->evp, tmp->item);
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

extern FMFormat
INT_EVget_src_ref_format(EVsource source)
{
    return source->reference_format;
}

extern int
INT_EVfreeze_stone(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    stone = stone_struct(evp, stone_id);
    if (!stone) return -1;
    stone->is_frozen = 1;
    return 1;	
}

extern int
INT_EVunfreeze_stone(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    stone = stone_struct(evp, stone_id);
    if (!stone) return -1;

    stone->is_frozen = 0;
    /* ensure that we run the process_actions loop soon so the stone's
       pending events (or pending output) won't be ignored */
    (void) INT_CMadd_delayed_task(cm, 0, 0, deferred_process_actions, NULL);
    return 1;	
}

extern int
INT_EVdrain_stone(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
/*    attr_list stone_attrs;
    EVevent_list buf_list = NULL;*/

    stone = stone_struct(evp, stone_id);
    if (!stone) return -1;

    stone->is_draining = 1;
    while(stone->is_processing || stone->is_outputting);
    stone->is_draining = 2;
    return 1;
}

extern EVevent_list
INT_EVextract_stone_events(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    EVevent_list list = malloc(sizeof(list[0]));

    list[0].length = -1;
    stone = stone_struct(evp, stone_id);
    if (!stone) return NULL;
    list = extract_events_from_queue(cm, stone->queue, list);
    return list;
}

extern attr_list
INT_EVextract_attr_list(CManager cm, EVstone stone_id)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    stone = stone_struct(evp, stone_id);
    if (!stone) return NULL;
    return(stone->stone_attrs);
}

extern void
INT_EVset_attr_list(CManager cm, EVstone stone_id, attr_list stone_attrs)
{
    event_path_data evp = cm->evp;
    stone_type stone;
    stone = stone_struct(evp, stone_id);
    if (!stone) return;
    stone->stone_attrs = stone_attrs;
    add_ref_attr_list(stone_attrs);
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
    stone = stone_struct(evp, stone_id);
    if (!stone) return -1;
    while (stone->is_draining != 2);  
    empty_queue(stone->queue);
    free(stone->queue);
    if (stone->response_cache) free(stone->response_cache);
    INT_EVfree_stone(cm, stone_id);  
    return 1;      
} 
    


