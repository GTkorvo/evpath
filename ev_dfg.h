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

/*!
 * EVdfg is a handle to a DFG.
 *
 * EVdfg is an opaque handle
 */
typedef struct _EVdfg *EVdfg;

/*!
 * EVdfg_stone is a handle to virtual EVpath stone.
 *
 * EVdfg_stone is an opaque handle.  
 */
typedef struct _EVdfg_stone *EVdfg_stone;

/*!
 * Create a DFG
 *
 * This call is used in both master and client sides of EVdfg.
 * \param cm The CManager with which to associate the DFG
 * \return An EVdfg handle, to be used in later calls.
 */
extern EVdfg EVdfg_create(CManager cm);

/*!
 * Get the contact list from an EVdfg handle
 *
 * This call is used to extract contact information from an EVdfg handle.
 * Generally this call is made on the Master side of EVdfg, and the contact
 * information is then provided to the Clients for use in EVdfg_join_dfg()
 * calls.  The result of this call is a null-terminated string to be owned
 * by the caller.  (I.E. you should free the string memory when you're done
 * with it.)
 *
 * \param dfg The EVdfg handle for which to create contact information.
 * \return A null-terminated string representing contact information for this EVdfg
 */
extern char *EVdfg_get_contact_list(EVdfg dfg);


/*!
 * Join an EVdfg
 *
 *  This call is used to join a DFG as a client, though it is also typically
 *  employed by the master to join the previously created DFG.  It is used
 *  by both non-master clients and by the master to participate in the DFG
 *  is has created.  In all cases, the master_contact string should be the
 *  same one that came from EVdfg_get_contact_list() on the master.
 *
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

/*!
 *  Supply a static list of client names to the EVdfg master.
 *
 *   This call is used in static joining mode, that is when the set of nodes
 * that will join the DFG is known upfront and each has a predefined unique 
 * name. 
 * \param dfg The local EVdfg handle for which the set of clients is to be
 * specified. 
 * \param list A NULL-terminated list of NULL-terminated strings.  These
 * names must be unique, and each must be used in an EVdfg_join_dfg() call
 * before the ensemble will be complete.  EVdfg copies this list, so it
 * should be free'd by the calling application if dynamic.
 *
 */
extern void EVdfg_register_node_list(EVdfg dfg, char** list);

/*!
 * The prototype for an EVdfg client-join handling function.
 *
 * In "dynamic join" mode, as opposed to static-client-list mode (See
 * EVdfg_register_node_list()), EVdfg calls a registered join handler each
 * time a new client node joins the DFG.  This handler should 1) potentially
 * assign a canonical name to the client node (using
 * EVdfg_assign_canonical_name()), and 2) when the expected nodes have all
 * joined the handler should create the virtual DFG and then call
 * EVdfg_realize() in order to instantiate it.
 * 
 * This call happens in the Master process only.
 * 
 * \param dfg The EVdfg handle with which this handler is associated
 * \param identifier This null-terminated string is the value specified in
 * the corresponding EVdfg_join_dfg() call by a client.
 * \param available_sources This parameter is currently a placeholder for
 * information about what sources the client is capable of hosting.
 * \param available_sinks This parameter is currently a placeholder for
 * information about what sinks the client is capable of hosting.
 */
typedef void (*EVdfgJoinHandlerFunc) (EVdfg dfg, char *identifier, void* available_sources, void *available_sinks);

/*!
 * The prototype for an EVdfg client-fail handling function.
 *
 * If the an EVdfgFailHandlerFunc has been registered to the DFG, this
 * function will be called in the event that some node has failed.
 * Generally EVdfg becomes aware that a node has failed when some other
 * client tries to do a write to a bridge stone connected to that node and
 * that write fails.  Three things to be cognizant of:
 *  - This call happens in the Master process only.
 *  - If it's the Master that has failed, you're not going to get a call.
 *  - If multiple nodes notice the same failure, you might get multiple
 * calls to this function resulting from a single failure.
 * 
 * \param dfg The EVdfg handle with which this handler is associated
 * \param identifier This null-terminated string is the value that was
 * specified in EVdfg_join_dfg() in the failed client.
 * \param reporting_stone This is local ID of the failed stone (perhaps not
 * useful to anyone - should change or eliminate this).
 */
typedef void (*EVdfgFailHandlerFunc) (EVdfg dfg, char *identifier, int reporting_stone);

/*!
 * The prototype for an EVdfg voluntary-reconfiguration handling function.
 *
 * If the an EVdfgReconfigHandlerFunc has been registered to the DFG, this
 * function will be called in the event that some stone has executed a call
 * to EVdfg_trigger_reconfiguration() inside a CoD-based event handler.
 * This allows a client application to monitor local conditions and to
 * trigger action by the master in the event a reconfiguration is called
 * for.  The EVdfg_trigger_reconfiguration() call and the
 * EVdfg_flush_attrs() call both cause local stone attributes to be flushed
 * back to the Master's virtual stones so that they can be examined using
 * EVdfg_get_attr_list().
 * 
 * \param dfg The EVdfg handle with which this handler is associated
 */
typedef void (*EVdfgReconfigHandlerFunc) (EVdfg dfg);

/*!
 * Register a node join handler function to an EVdfg.
 *
 * \param dfg The EVdfg handle with which to associate this handler
 * \param func The handler function to associate
 */
extern void EVdfg_node_join_handler (EVdfg dfg, EVdfgJoinHandlerFunc func);

/*!
 * Register a node fail handler function to an EVdfg.
 *
 * \param dfg The EVdfg handle with which to associate this handler
 * \param func The handler function to associate
 */
extern void EVdfg_node_fail_handler (EVdfg dfg, EVdfgFailHandlerFunc func);

/*!
 * Register a voluntary reconfiguration handler function to an EVdfg.
 *
 * \param dfg The EVdfg handle with which to associate this handler
 * \param func The handler function to associate
 */
extern void EVdfg_node_reconfig_handler (EVdfg dfg, EVdfgReconfigHandlerFunc func);

/*!
 * Cause the instantiation of a virtual DFG.
 *
 *  This call is performed by the master to signal the end of the creation
 *  or reorganization of a DFG.  In static-client-list mode, it is generally
 *  called by master after the virtual DFG has been created and just before
 *  the master calls EVdfg_join_dfg().  In dynamic-join mode, creating the
 *  virtual DFG and calling EVdfg_realize() is how the join handler signals
 *  EVdfg that all expected nodes have joined.  In a node fail handler, a
 *  node reconfig handler, or when the join handler is called after the
 *  first realization of the DFG, a further call of EVdfg_realize()
 *  represents the finalization of a reconfiguration of the DFG.
 *
 * \param dfg The handle of the EVdfg to be realized.
 */
extern int EVdfg_realize(EVdfg dfg);

/*!
 * Wait for a DFG to be ready to run
 *
 *  This call is performed by nodes which are participating in the DFG
 *  (clients and master) in order to wait for all nodes to join and the DFG
 *  to be realized.  This call will only return after the DFG has been
 *  completely instantiated and is running.  All participating clients will
 *  exit this call at roughly the same time.
 *
 * \param dfg The EVdfg handle upon which to wait.
 */
extern int EVdfg_ready_wait(EVdfg dfg);

/*!
 * Associate a name with a source handle
 *
 *  This call is performed by client nodes in order to register their
 *  ability to host a source with the given name.  If the name appears in
 *  a virtual source stone that is mapped to the client, then that source
 *  handle is `active`. 
 *
 * \param name The name to associate with the source handle.  EVdfg does not
 *  take ownership of this string and the application should ensure that the
 *  string remains valid for the duration of EVdfg operation.
 * \param src The EVsource to be associated with the name.  Source/name
 *  association is actually an EVPath-level operation, so there is no EVdfg
 *  parameter in this call.
 */
extern void
EVdfg_register_source(char *name, EVsource src);

/*!
 * Associate a name with a sink handle
 *
 *  This call is performed by client nodes in order to register their
 *  ability to host a sink stone with a handler with the given name.  
 *
 * \param cm The client's CManager
 * \param name The name to associate with the sink handle.  EVdfg does not
 *  take ownership of this string and the application should ensure that the
 *  string remains valid for the duration of EVdfg operation.
 * \param list The FMStructDescList describing the data that this handler
 *  function expects.  EVdfg does not take ownership of this data structure
 *  and the application should ensure that the structure remains valid for
 *  the duration of EVdfg operation.
 * \param handler The EVSimpleHandlerFunc to be associated with the
 *  name/data type.  Sink-handle/name association is actually an
 *  EVPath-level operation, so there is a CM parameter in this call, but no
 *  EVdfg param.
 * \param client_data An uninterpreted value that is passed to the handler
 * function when it is called.
 */
extern void
EVdfg_register_sink_handler(CManager cm, char *name, FMStructDescList list, EVSimpleHandlerFunc handler, void* client_data);

/*!
 * Associate a name with a raw sink handle
 *
 *  This call is performed by client nodes in order to register their
 *  ability to host a sink stone with a raw handler with the given name.  
 *
 * \param cm The client's CManager
 * \param name The name to associate with the sink handle.  EVdfg does not
 *  take ownership of this string and the application should ensure that the
 *  string remains valid for the duration of EVdfg operation.
 * \param handler The EVRawHandlerFunc to be associated with the
 *  name/data type.  Sink-handle/name association is actually an
 *  EVPath-level operation, so there is a CM parameter in this call, but no
 *  EVdfg param.
 */
extern void
EVdfg_register_raw_sink_handler(CManager cm, char *name, EVRawHandlerFunc handler);

/*!
 *  return a boolean describing whether the source has been assigned in EVdfg
 *
 *  This call is performed by client nodes in order to determine if a
 *  particular local EVsource registered with EVdfg has actually been made
 *  active by having a virtual source stone associated with it.
 *
 * \param src The source to test
 */
extern int EVdfg_source_active(EVsource src);

/*!
 *  return a count of EVdfg sink stones assigned to the current client
 *
 *  This call is performed by client nodes in order to determine how many
 *  sink stones have been assigned to them.
 *
 * \param dfg The DFG under consideration.
 */
extern int EVdfg_active_sink_count(EVdfg dfg);

/*!
 *  Assign a unique, canonical name to a client of a particular given_name.
 *
 *  This call is performed by the master, typically in the
 *  EVdfgJoinHandlerFunc, in order to assign a unique name to clients who may
 *  not have one previously.  The canonical name is the name to be used in
 *  EVdfg_assign_node(). 
 *
 * \param dfg The DFG under consideration.
 * \param given_name The original name of the client.
 * \param canonical_name The canonical name to be assigned to the client.
 */
extern void EVdfg_assign_canonical_name(EVdfg dfg, char *given_name, char *canonical_name);

/*!
 *  Create an EVdfg stone with a specific action associated with it.
 *
 *  This call is performed by the master during the Stone Creation and
 *  Assignment process in order to create an EVdfg_stone.
 *
 * \param dfg The DFG under consideration.
 * \param action_spec An action specifier string such as is created by the
 *  EVpath create_*_action_spec() routines.  This parameter may be NULL if
 *  no action is to be initially assigned to the stone.  If non-NULL, EVdfg
 *  takes ownership of the action_spec string and will free it upon DFG
 *  termination. 
 * \return Function returns an EVdfg_stone handle that can be used in
 *  subsequent calls.
 */
extern EVdfg_stone EVdfg_create_stone(EVdfg dfg, char *action_spec);

/*!
 *  Add an action to an existing EVdfg stone.
 *
 *  This call is performed by the master during the Stone Creation and
 *  Assignment process in order to add an action to an EVdfg_stone. 
 *
 * \param stone The EVdfg_stone to which to add the action.
 * \param action_spec An action specifier string such as is created by the
 *  EVpath create_*_action_spec() routines.  EVdfg takes ownership of the
 *  action_spec string and will free it upon DFG termination. 
 */
extern void EVdfg_add_action (EVdfg_stone stone, char *action_spec);

/*!
 *  Create an EVdfg stone that will act as an Event source.
 *
 *  This call is performed by the master during the Stone Creation and
 *  Assignment process in order to create an EVdfg source stone.  No other
 *  actions should be assigned to an EVdfg source stone.
 *
 * \param dfg The DFG under consideration.
 * \param source_name This value must match some value which has been
 *  specified in EVdfg_register_source() on the node to which this stone is
 *  eventually mapped.  (EVdfg can't detect a mismatch until EVdfg_realize()
 *  is called.)  The source_name string is *not* owned by EVdfg and should
 *  be free'd by the application if dynamic.
 * \return Function returns an EVdfg_stone handle that can be used in
 *  subsequent calls.
 */
extern EVdfg_stone EVdfg_create_source_stone(EVdfg dfg, char *source_name);

/*!
 *  Create an EVdfg stone that will act as an Event sink (terminal stone).
 *
 *  This call is performed by the master during the Stone Creation and
 *  Assignment process in order to create an EVdfg sink stone.
 *
 * \param dfg The DFG under consideration.
 * \param handler_name This value must match some value which has been
 *  specified in EVdfg_register_sink_handler() on the node to which this
 *  stone is eventually mapped.  (EVdfg can't detect a mismatch until
 *  EVdfg_realize() is called.)  The handler_name string is *not* owned by
 *  EVdfg and should be free'd by the application if dynamic.
 * \return Function returns an EVdfg_stone handle that can be used in
 *  subsequent calls.
 */
extern EVdfg_stone EVdfg_create_sink_stone(EVdfg dfg, char *handler_name);

/*!
 *  Add a sink action to an existing EVdfg stone.
 *
 *  This call is performed by the master during the Stone Creation and
 *  Assignment process in order to add a sink (terminal) action to an EVdfg
 *  stone. 
 *
 * \param stone The EVdfg_stone to which to add the sink action.
 * \param handler_name This value must match some value which has been
 *  specified in EVdfg_register_sink_handler() on the node to which this
 *  stone is eventually mapped.  (EVdfg can't detect a mismatch until
 *  EVdfg_realize() is called.)  The handler_name string is *not* owned by
 *  EVdfg and should be free'd by the application if dynamic.
 */
extern void EVdfg_add_sink_action(EVdfg_stone stone, char *handler_name);

/*!
 * Link a particular output port of one stone (the source) to a destination
 * (target) stone.
 * 
 * This function is roughly the analog of the EVstone_set_output function, but at
 * the EVdfg level.  All non-terminal stones have one or more output ports
 * from which data will emerge.  EVdfg_link_port() is used to assign each of
 * these outputs to another EVdfg_stone stone.
 *
 * \param source The EVdfg_stone whose ports are to be assigned.
 * \param output_index The zero-based index of the output which should be assigned.
 * \param destination The EVdfg_stone which is to receive those events.
 */
extern void EVdfg_link_port(EVdfg_stone source, int output_index, 
			    EVdfg_stone destination);

/*!
 * Assign a particular EVdfg_stone to a particular client node.
 * 
 * This function assigns a particular EVdfg_stone to be instantiated upon a
 *  particular client node.  The node parameter must match either 1) a
 *  string specified in EVdfg_register_node_list() (in static joining mode),
 *  or 2) a canonical name that has been assigned to a node (in dynamic
 *  joining mode).
 *
 * \param stone The EVdfg_stone to be assigned to a particular node.
 * \param node The node to which it is to be assigned.  EVdfg does not take
 *  ownership of this string and it should be free'd by the application if dynamic.
 */
extern void EVdfg_assign_node(EVdfg_stone stone, char *node);

/*!
 * Enable periodic auto-submits of NULL events on an EVdfg_stone. 
 * 
 * This function is the analog of the EVenable_auto_stone() function, but at
 * the EVdfg level.
 *
 * \param stone The EVdfg_stone which should receive auto-submits.
 * \param period_sec The period at which submits should occur, seconds portion.
 * \param period_usec The period at which submits should occur, microseconds
 * portion.
 *  
 * Autosubmits are intiated on each node just as it is about to return from
 * EVdfg_ready_wait().
 */
extern void EVdfg_enable_auto_stone(EVdfg_stone stone, int period_sec, 
				    int period_usec);

/*!
 * Set the attribute list that will be visible as "stone_attrs" inside EVdfg
 *  dynamic functions.
 * 
 * \param stone The EVdfg_stone affected.
 * \param attrs The attribute list to be assigned.  EVdfg does an
 *  add_ref_attr_list() on this list.  Because lists are only free'd when
 *  the reference count goes to zero, the application should generally call
 *  free_attr_list() on the list as well.
 */
extern void EVdfg_set_attr_list(EVdfg_stone stone, attr_list attrs);

/*!
 * Query the attribute list that is/was visible as "stone_attrs" inside EVdfg
 *  dynamic functions.
 * 
 * \param stone The EVdfg_stone involved.
 * \return The "stone_attrs" attribute list.  EVdfg does an
 *  add_ref_attr_list() on this list before returning it.  Because lists are
 *  only free'd when the reference count goes to zero, the application
 *  should generally call free_attr_list() when it is finished with it.  
 *  Unless dynamic reconfiguration is in play, the stone_attrs value
 *  reported here does not reflect updates from the instantiated stones on
 *  the client nodes (even if local to the master).  However, for voluntary
 *  reconfiguration, instance stone attributes are flushed to the master and
 *  can be interrogated with this call.
 */
extern attr_list EVdfg_get_attr_list(EVdfg_stone stone);


/*!
 *  Vote that this node is ready for shutdown and provide it's contribution
 *  to the return value.
 *
 *  One of EVdfg_shutdown() or EVdfg_ready_for_shutdown() should be called
 *  by every participating node for normal shutdown.  The return value from
 *  EVdfg_wait_for_shutdown() will be the same on each node and will depend
 *  upon every node's contribution to the shutdown status.
 *
 * \param dfg The local EVdfg handle for which shutdown is indicated.
 * \param result this node's contribution to the DFG-wide shutdown value
 *
 */
extern int EVdfg_shutdown(EVdfg dfg, int result);

/*!
 *  Vote that this node is ready for shutdown and is providing no specific contribution
 *  to the return value.
 *
 *  One of EVdfg_shutdown() or EVdfg_ready_for_shutdown() should be called
 *  by every participating node for normal shutdown.  The return value from
 *  EVdfg_wait_for_shutdown() will be the same on each node and will depend
 *  upon every node's contribution to the shutdown status.
 *
 * \param dfg The local EVdfg handle for which shutdown is indicated.
 *
 */
extern void EVdfg_ready_for_shutdown(EVdfg dfg);

/*!
 *  Wait for EVdfg to determine that the coordinated shutdown time has arrived.
 *
 *  One of EVdfg_shutdown() or EVdfg_ready_for_shutdown() should be called
 *  by every participating node for normal shutdown.  The return value from
 *  EVdfg_wait_for_shutdown() will be the same on each node and will depend
 *  upon every node's contribution to the shutdown status.
 *
 * \param dfg The local EVdfg handle for which shutdown is indicated.
 *
 */
extern int EVdfg_wait_for_shutdown(EVdfg dfg);

/*!
 *  Force EVdfg shutdown without necessarilying having contributions from all nodes.
 *
 *  Generally this will cause every call to EVdfg_wait_for_shutdown() to
 *  return the result value here, terminating execution of the EVdfg.
 *
 * \param dfg The local EVdfg handle for which shutdown is indicated.
 * \param result this node's contribution to the DFG-wide shutdown value
 *
 */
extern int EVdfg_force_shutdown(EVdfg dfg, int result);
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

/* @}*/

#ifdef	__cplusplus
}
#endif

#endif
