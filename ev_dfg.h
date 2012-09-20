#ifndef __EV_DFG__H__
/*! \file */

#include "evpath.h"

#ifdef	__cplusplus
extern "C" {
#endif
/*
**  Basic approach:
**  Create a DFG of "virtual stones" to be later deployed.
**  - Associate actions (created with our usual create action routines from StructDecls and text actions)
**  - Associate attributes
**  Actual deployment:
**  - first version:
**    * Centralized
**    * Server or distinguished participant is informed of all participants,  he gives them DFG segments and gets 
**      reports back of stone IDs, necessary stone IDs are then distributed to neighbors and the cohort is released.
**    * require all participants to be present before continuing
**    * manage "ready messages" so that DFG is realized without race conditions
**    * Do "validation" of DFG pre-realization.  Check for disconnected vertices, look at data types to see if they
**      will be handled, etc.
**    * deployment master may be distinguished participant or separate server.
**  - optimization
**    * eliminate actual stone IDs.  As long as the number of stones per CM is relatively small, we can use DFG-assigned 
**      stone IDs in the event-message and do a table lookup to determine the right local stone when the message arrives.
**  - next version
**    * add nodes to (fixed) DFG as they come up.  Provide early-arrivers with the contact lists of late-arriving 
**      neighbors as they join.
*/

typedef struct _EVdfg *EVdfg;
typedef struct _EVdfg_stone *EVdfg_stone;

/* 
**  Calls to create the actual DFG.
**  These calls happen in the master/distinguished node.
*/
extern EVdfg EVdfg_create(CManager cm);
extern char *EVdfg_get_contact_list(EVdfg dfg);
extern EVdfg_stone EVdfg_create_stone(EVdfg dfg, char *action_spec);
extern void EVdfg_add_action (EVdfg_stone stone, char *action_spec);
extern EVdfg_stone EVdfg_create_source_stone(EVdfg dfg, char *source_name);
extern EVdfg_stone EVdfg_create_sink_stone(EVdfg dfg, char *handler_name);
extern void EVdfg_add_sink_action(EVdfg_stone stone, char *handler_name);
extern void EVdfg_enable_auto_stone(EVdfg_stone stone, int period_sec, 
				    int period_usec);

extern void EVdfg_link_port(EVdfg_stone source, int source_port, 
			    EVdfg_stone destination);
extern void EVdfg_set_attr_list(EVdfg_stone stone, attr_list attrs);

extern void EVdfg_assign_node(EVdfg_stone stone, char *node);
extern void EVdfg_register_node_list(EVdfg dfg, char** list);
extern void EVdfg_assign_canonical_name(EVdfg dfg, char *given_name, char *canonical_name);
typedef int (*EVdfgJoinHandlerFunc) (EVdfg dfg, char *identifier, void* available_sources, void *available_sinks);
typedef void (*EVdfgFailHandlerFunc) (EVdfg dfg, char *identifier, int reporting_stone);
extern void EVdfg_node_join_handler (EVdfg dfg, EVdfgJoinHandlerFunc func);
extern void EVdfg_node_fail_handler (EVdfg dfg, EVdfgFailHandlerFunc func);

extern int EVdfg_realize(EVdfg dfg);
extern int EVdfg_ready_wait(EVdfg dfg);
extern void EVdfg_join_dfg(EVdfg dfg, char *node_name, char *master_contact);

extern int EVdfg_shutdown(EVdfg dfg, int result);
extern int EVdfg_wait_for_shutdown(EVdfg dfg);

/*
  (VERY) tentative reconfiguration interface
*/

extern void EVdfg_reconfig_insert(EVdfg dfg, int src_stone_id, EVdfg_stone new_stone, int dest_stone_id, EVevent_list q_event);
extern void EVdfg_reconfig_delete_link(EVdfg dfg, int src_index, int dest_index);
extern void EVdfg_freeze_next_bridge_stone(EVdfg dfg, int stone_index);
extern void EVdfg_freeze_next_bridge_stone(EVdfg dfg, int stone_index);
extern void EVdfg_reconfig_link_port_to_stone(EVdfg dfg, int src_stone_index, int port, EVdfg_stone target_stone, EVevent_list q_events);
extern void EVdfg_reconfig_link_port_from_stone(EVdfg dfg, EVdfg_stone src_stone, int port, int target_index, EVevent_list q_events);
extern void EVdfg_reconfig_link_port(EVdfg_stone src, int port, EVdfg_stone dest, EVevent_list q_events);
extern void EVdfg_reconfig_transfer_events(EVdfg dfg, int src_stone_index, int src_port, int dest_stone_index, int dest_port);
extern void EVdfg_reconfig_insert_on_port(EVdfg dfg, EVdfg_stone src_stone, int port, EVdfg_stone new_stone, EVevent_list q_events);

/* 
**  Calls that support the begin/end points.  
**  All those below must happen in the actual cohort 
*/
extern void
EVdfg_register_source(char *name, EVsource src);

extern void
EVdfg_register_sink_handler(CManager cm, char *name, FMStructDescList list, EVSimpleHandlerFunc handler);

extern void
EVdfg_register_raw_sink_handler(CManager cm, char *name, EVRawHandlerFunc handler);

extern void EVdfg_ready_for_shutdown(EVdfg dfg);
extern int EVdfg_source_active(EVsource src);
extern int EVdfg_active_sink_count(EVdfg dfg);

#ifdef	__cplusplus
}
#endif

#endif
