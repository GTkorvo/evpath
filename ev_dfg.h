#ifndef __EV_DFG__H__
/*! \file */

#include "evpath.h"

#ifdef	__cplusplus
extern "C" {
#endif
/** @defgroup ev_dfg EVdfg functions and types
 * @{
 */
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

/*!
 * Create a DFG
 *
 * This call is used in both master and client sides of EVdfg.
 * \param cm The CManager with which to associate the DFG
 * \return An EVdfg handle, to be used in later calls.
 */
extern EVdfg EVdfg_create(CManager cm);

/*
 * Get the contact list from an EVdfg handle
 *
 * This call is used to extract contact information from an EVdfg handle.
 * Generally this call is made on the Master side of EVdfg, and the contact
 * information is then provided to the Clients for use in EVdfg_join_dfg()
 * calls.  The result of this call is a null-terminated string to be owned
 * by the caller.  (I.E. you should free the string memory when you're done
 * with it.)
 * \param dfg The EVdfg handle for which to create contact information.
 * \return A null-terminated string representing contact information for this EVdfg
 */
extern char *EVdfg_get_contact_list(EVdfg dfg);


/*!
 * Join an EVdfg
 *
 *  This call is used to join a DFG as a client, though it is also typically
 *  employed by the master to join the previously created DFG.  In
 * \param dfg The local EVdfg handle which should join the global DFG.
 * \param node_name The name with which the client can be identified.  This
 *  should be unique among the joining nodes in static joining mode
 *  (I.E. using EVdfg_register_node_list().  In dynamic mode (I.E. where
 *  EVdfg_node_join_handler() is used), then this name is presented to the
 *  registered join handler, but it need not be unique.  EVdfg copies this
 *  string, so it can be free'd after use.
 * \param master_contact The string contact information for the master
 *  process.  This is not stored by EVdfg.
 *
 */
extern void EVdfg_join_dfg(EVdfg dfg, char *node_name, char *master_contact);

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
extern attr_list EVdfg_get_attr_list(EVdfg_stone stone);

extern void EVdfg_assign_node(EVdfg_stone stone, char *node);
extern void EVdfg_register_node_list(EVdfg dfg, char** list);
extern void EVdfg_assign_canonical_name(EVdfg dfg, char *given_name, char *canonical_name);
typedef int (*EVdfgJoinHandlerFunc) (EVdfg dfg, char *identifier, void* available_sources, void *available_sinks);
typedef void (*EVdfgFailHandlerFunc) (EVdfg dfg, char *identifier, int reporting_stone);
typedef void (*EVdfgReconfigHandlerFunc) (EVdfg dfg);
extern void EVdfg_node_join_handler (EVdfg dfg, EVdfgJoinHandlerFunc func);
extern void EVdfg_node_fail_handler (EVdfg dfg, EVdfgFailHandlerFunc func);
extern void EVdfg_node_reconfig_handler (EVdfg dfg, EVdfgReconfigHandlerFunc func);

extern int EVdfg_realize(EVdfg dfg);
extern int EVdfg_ready_wait(EVdfg dfg);

extern int EVdfg_shutdown(EVdfg dfg, int result);
extern int EVdfg_force_shutdown(EVdfg dfg, int result);
extern int EVdfg_wait_for_shutdown(EVdfg dfg);
extern void EVdfg_ready_for_shutdown(EVdfg dfg);

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

extern int EVdfg_source_active(EVsource src);
extern int EVdfg_active_sink_count(EVdfg dfg);

/* @}*/

#ifdef	__cplusplus
}
#endif

#endif
