
#ifndef __EVPATH__H__
#define __EVPATH__H__
/*! \file */

#if defined(FUNCPROTO) || defined(__STDC__) || defined(__cplusplus) || defined(c_plusplus)
#ifndef ARGS
#define ARGS(args) args
#endif
#else
#ifndef ARGS
#define ARGS(args) (/*args*/)
#endif
#endif

#include "io.h"
#include "atl.h"
#ifdef	__cplusplus
extern "C" {
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif


/*!
 * A structure to hold Format Name / Field List associations.
 *
 *
 *  This is used to associate type names with type descriptions (field lists).
 *  Together these define structure types that can be composed into larger 
 *  structures.  A CMFormatList should be the transitive closure of the
 *  structure types that are included in the first structure type
 *  (CMFormatList[0]);  The list is terminated with a {NULL, NULL}.
 */
struct _CMformat_list {
    /*! the name to be associated with this structure */
    char *format_name;
    /*! the PBIO-style list of fields within this structure */
    IOFieldList field_list;
};
/* The above exist for compatibility reasons -sandip */

/*!
 * A structure to hold Format Name / Field List associations.
 *
 *
 *  This is used to associate names with field lists.  Together these define 
 *  a structure that can be composed into larger structures.
 *  This is used to associate type names with type descriptions (field lists).
 *  Together these define structure types that can be composed into larger 
 *  structures.  A CMFormatList should be the transitive closure of the
 *  structure types that are included in the first structure type
 *  (CMFormatList[0]);  The list is terminated with a {NULL, NULL}.
 */
typedef struct _IOformat_list CMFormatRec;

/*!
 * A list of CMFormatRec structures.
 *
 * In its use in CM, a CMFormatList represents the transitive closure of
 * substructures that compose a larger structure.  The name/field list entry
 * for each particular format should appear before it is used (in the field
 * lists of later entries).  This implies that the first entry has only
 * fields which have atomic data types.
 */
typedef CMFormatRec *CMFormatList;

struct _CManager;
struct _CMConnection;
struct _CMFormat;

/*!
 * CManager is the root of control flow and message handling in a CM program.
 *
 * CManager is an opaque handle.  
 */
typedef struct _CManager *CManager;

/*!
 * CMConnection is a handle to a communications link.
 *
 * CManager is an opaque handle.  
 */
typedef struct _CMConnection *CMConnection;

/*!
 * CMFormat is a handle to a registered native format.
 *
 * CMFormat is an opaque handle.  It is the return value from
 * CMregister_format() and is used both to identify data for writing (in
 * CMwrite() and CMwrite_attr() and to register handlers for incoming data
 * (with CMregister_handler()).
 */
typedef struct _CMFormat *CMFormat;

/*!
 * CMTaskHandle is a handle to a delayed or periodic task.
 */
typedef struct _CMTaskHandle *CMTaskHandle;

/*!
 * buf_entry is a structure used to return the lengths of encoded events
 * and a pointer to their locations.
 */
typedef struct buf_entry {
    int length;
    void *buffer;
} *EVevent_list;

/*!
 * The prototype for a CM data handling function.
 *
 * CM allows application-routines matching this prototype to be registered
 * as data handlers.
 * \param cm The CManager with which this handler was registered.
 * \param conn The CMConnection upon which the message arrived.
 * \param message A pointer to the incoming data, cast to void*.  The real
 * data is formatted to match the fields of with which the format was
 * registered. 
 * \param client_data This value is the same client_data value that was
 * supplied in the CMregister_handler() call.  It is not interpreted by CM,
 * but instead can be used to maintain some application context.
 * \param attrs The attributes (set of name/value pairs) that this message
 * was delivered with.  These are determined by the transport and may
 * include those specified in CMwrite_attr() when the data was written.
 */
typedef void (*CMHandlerFunc) ARGS((CManager cm, 
				    CMConnection conn,
				    void *message, void *client_data,
				    attr_list attrs));

/*!
 * The prototype for a CM polling handler (and others).
 *
 * Functions matching of this prototype can be registered with CMadd_poll(),
 * CMadd_periodic_task(), CMadd_delayed_task() and CMadd_shutdown_task().
 * \param cm The CManager with which this handler was registered.
 * \param client_data This value is the same client_data value that was
 * supplied in the CMadd_poll() call.  It is not interpreted by CM,
 * but instead can be used to maintain some application context.
 */
typedef void (*CMPollFunc) ARGS((CManager cm, void *client_data));

/*!
 * The prototype for a CM connection close handler.
 *
 * Functions matching of this prototype can be registered with
 * CMregister_close_handler(). 
 * \param cm The CManager with which this handler was registered.
 * \param conn The CMConnection which is being closed.
 * \param client_data This value is the same client_data value that was
 * supplied in the CMregister_close_handler() call.  It is not interpreted 
 * by CM, but instead can be used to maintain some application context.
 */
typedef void (*CMCloseHandlerFunc) ARGS((CManager cm, CMConnection conn,
					 void *client_data));

/*!
 * The prototype for a CM write possible callback.
 *
 * Functions matching of this prototype can be registered with
 * CMregister_write_callback(). 
 * \param cm The CManager with which this callback function was registered.
 * \param conn The CMConnection upon which a non-blocking write is now (potentially) possible. 
 * \param client_data This value is the same client_data value that was
 * supplied in the CMregister_write_callback() call.  It is not interpreted 
 * by CM, but instead can be used to maintain some application context.
 */
typedef void (*CMWriteCallbackFunc) ARGS((CManager cm, CMConnection conn,
					  void *client_data));


/*!
 * create a CManager.
 *
 * CManager is the root of control flow and message handling in a CM program.
 */
/*NOLOCK*/
extern CManager CManager_create();

/*!
 * close a CManager
 *
 * the close operation shuts down all connections and causes the
 * termination of any network handling thread.
 * \param cm The CManager to be shut down.
 */
extern void CManager_close ARGS((CManager cm));

/*!
 * fork a thread to handle the network input operations of a CM.
 *
 * \param cm The CManager whose input should be handled by a new thread.
 * \return 
 * - 0 if a communications manager thread cannot be forked
 * - 1 success
 * \warning Only one thread should be handling input for a CM.  If this call
 * is used then no other thread should call CMpoll_network() or
 * CMrun_network(). 
 * \warning If this call is to be used (or indeed if any threading is to be
 * used), one of the gen_thread init routines should be called <b>before</b>
 * the call to CManager_create().  Otherwise bad things will happen.
 */
extern int CMfork_comm_thread ARGS((CManager cm));

/*!
 * Tell CM to listen for incoming network connections.
 *
 * \param cm The CManager which should listen.
 * \return the number of transports which successfully initiated connection
 * listen operations (by reporting contact attributes).
 * \note CMlisten() is identical to calling CMlisten_specific() with a NULL
 * value for the listen_info parameter.
 * \note The listening transports will add their contact information to the
 * list returned by CMget_contact_list().
 */
extern int CMlisten ARGS((CManager cm));

/*!
 * Tell CM to listen for incoming network connections with 
 * specific characteristics.
 *
 * \param cm The CManager which should listen.
 * \param listen_info An attribute list to be passed to the
 * transport-provided listen operations of all currently-loaded transports.
 * \return the number of transports which successfully initiated connection
 * listen operations (by reporting contact attributes).
 * \note The listen_info value is interpreted by each individual transport.
 * Currently implemented transports that use this include: 
 * - the <b>sockets</b> tranport which uses the #CM_IP_PORT attribute to control
 *   which port it listens on.  If this attribute is not present it listens
 *   on any available port. 
 * - the <b>rudp</b> tranport which uses the #CM_UDP_PORT attribute to control
 *   which port it listens on.  If this attribute is not present it listens
 *   on any available port. 
 * - the <b>atm</b> tranport which uses the #CM_ATM_SELECTOR and #CM_ATM_BHLI
 * attribute to control listens.  These attributes must be present.
 */
extern int CMlisten_specific ARGS((CManager cm, attr_list listen_info));

/*!
 * get the contact information for this CM.
 *
 * This call returns the set of attributes that define the contact
 * information for the network transports that have performed listen
 * operations.  
 * \param cm the CManager for which to return contact information.
 * \return the contact list.
 */
extern attr_list
CMget_contact_list ARGS((CManager cm));

/*!
 * insert contact information into this CM.
 *
 * This call adds to the set of attributes that define the contact
 * information for the network transports that have performed listen
 * operations.  
 * \param cm the CManager for which to add contact information.
 * \param attrs the information to add.
 */
extern void
CM_insert_contact_info ARGS((CManager cm, attr_list attrs));

/*!
 * get a specfic subset of the contact information for this CM.
 *
 * This call returns the set of attributes that define the contact
 * information for a particular network transport.  If no listen operation
 * has been performed on that transport, one will be done with a NULL attr
 * list. 
 * \param cm the CManager for which to return contact information.
 * \param attrs the attribute list specifying the transport.
 * \return the contact list.
 */
extern attr_list
CMget_specific_contact_list ARGS((CManager cm, attr_list attrs));

/*!
 * check to see if this is contact information for <b>this</b> CM.
 *
 * Since attribute lists are generally opaque, it is not necessarily obvious
 * when you have a set of contract attributes that is actually your own
 * contact list.  This call is designed to answer that question.
 *
 * \param cm The CManager whose contact information should be compared.
 * \param attrs The contact list to compare.
 * \return 1 if for some loaded transport the attrs list matches the contact
 * information in the cm. 0 othewise.
 */
extern int
CMcontact_self_check ARGS((CManager cm, attr_list attrs));

/*!
 * acquire a (possibly existing) connection to another CM process.
 *
 * \param cm The CManager in which to make the connection.
 * \param contact_list The attribute list specifying contact information for
 * the remote CM.
 * \return A CMConnection value, or NULL in the event of failure.
 *
 * CMget_conn() attempts to determine if the contact attribute match any
 * existing connection (Using the transport connection_eq() method).  If a
 * connection matches, that connection's reference count is incremented and
 * its value is returned.  If no connection matches, a CMinitiate_conn() is
 * performed using the contact list and its result value is returned.
 */
extern CMConnection
CMget_conn ARGS((CManager cm, attr_list contact_list));

/*!
 * initiate connection to another CM process.
 *
 * \param cm The CManager in which to make the connection.
 * \param contact_list The attribute list specifying contact information for
 * the remote CM.
 * \return A CMConnection value, or NULL in the event of failure.
 *
 * CMinitiate_conn() will attempt to initiate a connection using each of the
 * currently loaded transports.  It will return the first that succeeds, or
 * NULL if none succeed.
 * \note If the contact list contains a CM_TRANSPORT attribute with a string
 * value, CMinitiate_conn() will attempt to load that transport, then if that
 * succeeds will attempt to initiate a connection using only that transport.
 */
extern CMConnection
CMinitiate_conn ARGS((CManager cm, attr_list contact_list));

/*!
 * kill and potentially deallocate a connection.
 *
 * \param conn the CMConnection to kill
 *
 * CMConnection_close decrements the reference count of a connection.  If
 * the resulting reference count is zero, then the connection is shut down.
 * All resources associated with the connection are free'd, the close
 * handler is called and the CMConnection structure itself is free'd.
 * \warning CMConnection values should not be used after
 * CMConnection_close().  CMConnection_close() should only be used on 
 * CMConnection values created with CMget_conn() or CMinitiate_conn(), not 
 * with connections that are passively created (accepted through CMlisten()).
*/
extern void
CMConnection_close ARGS((CMConnection conn));

/*!
 * manually increment the reference count of a connection.
 *
 * \param conn the CMConnection whose count should be incremented.
 * \note  Used if some mechanism other than CMget_conn() is used to "share"
 * connection in multiple contexts so that it is closed only when all users
 * have closed it.
*/
extern void
CMConnection_add_reference ARGS((CMConnection conn));

/*!
 * register a function to be called when a connection is closed.
 *
 * \param conn the connection with which the function is associated.
 * \param func the function to be called when the connection closes.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 * \note There can be only one close handler per connection.  Multiple
 * registrations overwrite each other.
 */
extern void
CMconn_register_close_handler ARGS((CMConnection conn, 
				    CMCloseHandlerFunc func, 
				    void *client_data));
/*!
 * return the list of attributes associated with a connection.
 *
 * \param conn the connection for which to return the attributes.
 * \return an attr_list value containing connection attributes.
 */
extern attr_list 
CMConnection_get_attrs ARGS((CMConnection conn));

/*!
 * modify the characteristics of a connection.
 *
 * \param conn the connection for to modify characteristics.
 * \param attrs the characteristics to apply (specific to CM and transport).
 * \return a true/false failure value.
 */
extern int
CMConnection_set_character ARGS((CMConnection conn, attr_list attrs));

/*!
 * return connection 'i' associated with a CM value.
 *
 * \param cm the cmanager from which to return a connection.
 * \param i the index value into the CManager's list of connections.
 * \return a CMConnection value associated with connection 'i'
 */
extern CMConnection
CMget_indexed_conn ARGS((CManager cm, int i));

/*!
 * register a format with CM.
 *
 * \param cm  The CManager in which to register the format.
 * \param format_name The textual name to associate with the structure
 * format.
 * \param field_list The PBIO field list which describes the structure.
 * \param subformat_list A list of name/field_list pairs that specify the
 * representation of any nested structures in the message.  If the message
 * field types are simple pre-defined PBIO types, #subformat_list can be
 * NULL.  Otherwise it should contain the transitive closure of all data
 * types necessary to specify the message representation.  The list is
 * terminated with a <tt>{NULL, NULL}</tt> value.  
 *
 * Registering a format is a precursor to sending a message or registering a
 * handler for incoming messages.
 */
extern CMFormat
CMregister_format ARGS((CManager cm, char *format_name,
		       IOFieldList field_list, CMFormatList subformat_list));


/*!
 * register a format (with opt_info) with CM.
 *
 * \param cm  The CManager in which to register the format.
 * \param format_name The textual name to associate with the structure
 * format.
 * \param field_list The PBIO field list which describes the structure.
 * \param subformat_list A list of name/field_list pairs that specify the
 * representation of any nested structures in the message.  If the message
 * field types are simple pre-defined PBIO types, #subformat_list can be
 * NULL.  Otherwise it should contain the transitive closure of all data
 * types necessary to specify the message representation.  The list is
 * terminated with a <tt>{NULL, NULL}</tt> value.  
 * \param opt_info This specify the compatability info and/or XML info
 *
 * Registering a format is a precursor to sending a message or registering a
 * handler for incoming messages.
 */
extern CMFormat
CMregister_opt_format ARGS((CManager cm, char *format_name,
		       IOFieldList field_list, CMFormatList subformat_list,
		       IOOptInfo *opt_info));

/*!
 * Creates and returns compatabilty info.
 *
 * \param format  The CMFormat for which to create compatability info.
 * \param xform_code Code string that does the compatability conversion
 * \param len_p Length of returned buffer
 *
 * Creating compatability info is a precursor to CMregister_opt_format
 */
extern void *
CMcreate_compat_info ARGS((CMFormat format, char *xform_code, 
			int *len_p));

/*!
 * lookup the CMFormat associated with a particular IOFieldList
 *
 * \param cm The CManager in which the format was registered.
 * \param field_list The field list which was used in the registration.
 *
 * CMLookup_format() addresses a specific problem particular to libraries.
 * CMwrite() requires a CMFormat value that results from a
 * CMregister_format() call.  Efficiency would dictate that the
 * CMregister_format() be performed once and the CMFormat value used
 * repeatedly for multiple writes.  However, libraries which want to avoid
 * the use of static variables, or which wish to support multiple CM values
 * per process have no convenient way to store the CMFormat values for
 * reuse.   CMlookup_format() exploits the fact that field_lists are
 * often constants with fixed addresses (I.E. their memory is not reused for
 * other field lists later in the application).  This call quickly looks up
 * the CMFormat value in a CM by searching for a matching field_list
 * address. 
 * \warning You should *not* use this convenience routine if you cannot
 * guarantee that all field lists used to register formats have a unique
 * address. 
 */
extern CMFormat CMlookup_format ARGS((CManager cm, IOFieldList field_list));

/** @defgroup user User-level Format Functions
 * This is a set of functions related to handling "user formats".  I.E. PBIO
 * formats that might be used by the application or higher level library, but
 * which are *not* used for CM messages. 
 *
 * The formats registered using CMregister_format() are used to
 * encode messages that go directly over the network connections, or to
 * decode messages that come in over those connections.  PBIO manages these
 * formats, gathering formats that it needs and caching them for future use.
 *
 * However, CM also has facilities to support a second class of PBIO
 * formats, called <b>user formats</b>.  Applications using these facilities
 * can take advantage of CM's format management services to encode data that
 * is not transmitted directly over the network links.   
 *
 * To understand when this might be appropriate, consider an remote
 * procedure call library that allows its users to register procedure names
 * and parameter type profiles and make them available for remote access.
 * The library might be implemented in CM using a "RPC Request" message
 * with a the procedure to call specified by a string name and the
 * parameters to that procedure passed as a dynamically sized block of
 * characters.  This is a simple approach to library implementation, but how
 * should the parameter block be encoded?  Leaving the
 * marshalling/unmarshalling to the application is simple, but unappealing.
 * PBIO's IOContext routines were designed for precisely this sort of
 * situation, allowing data to be encoded into an arbitrary memory block,
 * transmitted elsewhere by any mechanism, and then decoded upon arrival.
 * The RPC library could use PBIO for this task independently of CM, but
 * then is loses out on the features that CM provides (such as each CM
 * acting as its own format server instead of using a common format server).
 *
 * CM's user formats support routines allow such a library to leverage CM's
 * format services while still largely using PBIO without interference.
 * Because these facilities are preliminary, subject to change, and unlikely
 * to be required except for the most advanced uses of CM, they are not
 * described here in detail.  Instead we just list the current interface.
 * Users requiring more information are encouraged to contact the author.
 * @{
 */
/*!
 *  get a PBIO IOContext to be used for user format operations
 * \param cm The CManager from which to acquire the user type context.
 */
extern
IOContext CMget_user_type_context ARGS((CManager cm));

/*!
 *  deallocate a PBIO IOContext acquired with CMget_user_type_context()
 * \param cm The CManager from which the context was acquired.
 * \param context the IOContext to free.
 */
extern
void CMfree_user_type_context ARGS((CManager cm, IOContext context));

/*!
 * register a user format with CM.
 *
 * \param cm  The CManager from which the IOContext was acquired.
 * \param context The IOContext with which to register the format.
 * \param format_name The textual name to associate with the structure
 * format.
 * \param field_list The PBIO field list which describes the structure.
 * \param subformat_list A list of name/field_list pairs that specify the
 * representation of any nested structures in the message.  If the message
 * field types are simple pre-defined PBIO types, #subformat_list can be
 * NULL.  Otherwise it should contain the transitive closure of all data
 * types necessary to specify the message representation.  The list is
 * terminated with a <tt>{NULL, NULL}</tt> value.  
 */
extern IOFormat
CMregister_user_format ARGS((CManager cm, IOContext context, char *format_name,
			      IOFieldList field_list, CMFormatList subformat_list));

/*!
 * lookup a user format based on the field list.
 * 
 * This call us the user-format analogue of CMlookup_format().
 * \param cm The CManager in which the format was registered.
 * \param field_list The field list which was used in the registration.
 * \return a PBIO IOFormat value
 * \note The return type differs from CMlookup_format() because we expect
 * the application to use PBIO directly for some calls.  For example,  
 * after a format is registered, PBIO encode routines can be used directly. 
 * \warning Using a PBIO-level calls and for which there is a corresponding
 * CM call may result in race conditions and unpleasant consequences.
 */
extern IOFormat CMlookup_user_format ARGS((CManager cm, IOFieldList field_list));

/*!
 * get a registered IOformat based on its simple name.
 *
 * \param cm The CManager in which the format was registered.
 * \param context The IOContext in which the format was registered.
 * \param name The name that was given the format when it was registered.
 * \return a PBIO IOFormat value.
 *
 * This call maps directly to the PBIO routine get_IOformat_by_name().  A CM
 * version is provided so that CM can protect data structures for thread
 * safety. 
 * \warning Using a PBIO-level calls and for which there is a corresponding
 * CM call may result in race conditions and unpleasant consequences.
 */
extern IOFormat CMget_IOformat_by_name ARGS((CManager cm, IOContext context,
					     char *name));
/*!
 * determine the IOFormat of data in an encoded buffer.
 *
 * \param cm The CManager from which the context was acquired.
 * \param context The IOContext to be used for determining the format.
 * \param buffer The buffer holding the encoded data.
 * \return The IOFormat value that describes the encoded data.
 *
 * This call maps directly to the PBIO routine get_format_IOcontext().  A CM
 * version is provided so that CM can protect data structures for thread
 * safety. 
 * \warning Using a PBIO-level calls and for which there is a corresponding
 * CM call may result in race conditions and unpleasant consequences.
 */
extern IOFormat CMget_format_IOcontext ARGS((CManager cm, IOContext context,
					     void *buffer));
/*!
 * determine the IOFormat of data in an encoded buffer, with context 
 * to recover self formats from.
 *
 * \param cm The CManager from which the context was acquired.
 * \param context The IOContext to be used for determining the format.
 * \param buffer The buffer holding the encoded data.
 * \param app_context Application context to pass to PBIO
 * \return The IOFormat value that describes the encoded data.
 *
 * This call maps directly to the PBIO routine get_format_app_IOcontext().  A CM
 * version is provided so that CM can protect data structures for thread
 * safety. 
 * \warning Using a PBIO-level calls and for which there is a corresponding
 * CM call may result in race conditions and unpleasant consequences.
 */
extern IOFormat CMget_format_app_IOcontext ARGS((CManager cm, IOContext context,
					     void *buffer, void *app_context));
/*!
 * determine the transitive closure of IOFormats used in data in an encoded
 * buffer. 
 *
 * \param cm The CManager from which the context was acquired.
 * \param context The IOContext to be used for determining the format.
 * \param buffer The buffer holding the encoded data.
 * \return A NULL-terminated list of IOFormat values that occur in the
 * encoded data.  The last entry will be the IOFormat that would be returned
 * by CMget_format_IOcontext().  Conversions can be registered in the order
 * they appear in the list.
 *
 * This call maps directly to the PBIO routine get_subformats_IOcontext().
 * A CM version is provided so that CM can protect data structures for
 * thread safety. 
 * \note The IOFormat* value returned should be released with CMfree().
 * \warning Using a PBIO-level calls and for which there is a corresponding
 * CM call may result in race conditions and unpleasant consequences.
 */
extern IOFormat *CMget_subformats_IOcontext ARGS((CManager cm, 
						  IOContext context,
						  void *buffer));
/*!
 * set a conversion (correspondence between wire and native formats) for an
 * incoming IOFormat.
 *
 * \param cm The CManager from which the context was acquired.
 * \param context The IOContext in which to register the conversion.
 * \param format The IOFormat for which to register a conversion.
 * \param field_list The IOFieldList describing the native structure.
 * \param native_struct_size The sizeof() value for the native structure.
 *
 * \note Once a conversion has been set, the PBIO decode routines can be
 * used directly.
 */
extern void
CMset_conversion_IOcontext ARGS((CManager cm, IOContext context,
				 IOFormat format, IOFieldList field_list,
				 int native_struct_size));
/**@} */

/*!
 * send a message on a connection.
 *
 * \param conn The CMConnection upon which to send the message.
 * \param format The CMFormat value returned by CMregister_format().
 * \param data The unencoded message, cast to a <tt>void*</tt> value.
 * \return
 * - 1 if the write was successful.
 * - 0 if the write did not complete successfully.
 * \note CMwrite() is equivalent to CMwrite_attr() with a NULL value 
 * passed for the attrs parameter.
 */
extern int
CMwrite ARGS((CMConnection conn, CMFormat format, void *data));

/*!
 * send a message on a connection with particular attributes.
 *
 * \param conn The CMConnection upon which to send the message.
 * \param format The CMFormat value returned by CMregister_format().
 * \param data The unencoded message, cast to a <tt>void*</tt> value.
 * \param attrs The set of name/value attributes with which to write the data.
 * \return
 * - 1 if the write was successful.
 * - 0 if the write did not complete successfully.
 * \note The values in the attrs parameter serve two purposes.  First, 
 * they may be interpreted by CM or the CM transport layers on either 
 * the writing or reading sides to customize event delivery.  Second, 
 * they are made available (perhaps with additional transport-specific 
 * attributes) to the read-side handler in the attrs argument to the 
 * CMHandlerFunc that handles the message.
 * \note CMwrite_attr() with a NULL value for the attrs parameter is 
 * equivalent to CMwrite().
 */
extern int
CMwrite_attr ARGS((CMConnection conn, CMFormat format, void *data, 
		   attr_list attrs));

/*!
 * register a function to be called when message matching a particular 
 * format arrives. 
 *
 * \param format The CMFormat value returned by CMregister_format()
 * \param handler The function to be called to handle the message.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 */
extern void
CMregister_handler ARGS((CMFormat format, CMHandlerFunc handler, 
			void *client_data));

/*!
 * register a function to be called when a write is again possible on a particular CMconnection.
 *
 * \param conn The CMConnection upon which to send the message.
 * \param handler The function to be called to handle the message.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 *
 * 
 */
extern void
CMregister_write_callback ARGS((CMConnection conn, 
				CMWriteCallbackFunc handler,
				void *client_data));

/*!
 * test whether a write to a particular connection would block
 *
 * \param conn The CMConnection to test
 * \return boolean TRUE(1) if the write would certainly block and 
 *   FALSE(0) if it may not.  
 * At the network level, we likely only know that <B>something</B> can be
 * sent, not how much.  So even if this returns false, a write may still
 * block at the network level.  If this happens, CM will copy the remaining
 * bytes and allow the CMwrite() to return, finishing the send
 * asynchronously.  However, if a CMwrite() is initiated when
 * write_would_block() is already TRUE, the write <b>will block</b> until
 * the blocking condition is gone (I.E. CMConnection_write_would_block() is
 * again FALSE).)
 */
extern int
CMConnection_write_would_block ARGS((CMConnection conn));

/*!
 * assume control over a incoming buffer of data.
 *
 * This call is designed to be used inside a CMHandlerFunc.  Normally data
 * buffers are recycled and CM only guarantees that the data delivered to a
 * CMHandlerFunc will be valid for the duration of the call.  In that
 * circumstance, a handler that wanted to preserve the data for longer than
 * its own duration (to pass it to a thread or enter it into some other data
 * structure for example) would have to copy the data.  To avoid that
 * inefficiency, CMtake_buffer() allows the handler to take control of the
 * buffer holding its incoming data.  The buffer will then not be recycled
 * until it is returned to CM with CMreturn_buffer().
 * \param cm The CManager in which the handler was called.
 * \param data The base address of the data (I.E. the message parameter to
 * the CMHandlerFunc).
 * \return NULL on error, otherwise returns the data parameter. 
*/
extern void *CMtake_buffer ARGS((CManager cm, void *data));

/*!
 * return a buffer of incoming data.
 *
 * This call recycles a data buffer that the application has taken control
 * of through CMtake_buffer().
 * \param cm The CManager in which the handler was called.
 * \param data The base address of the data (I.E. same value that was passed
 * to CMtake_buffer().
*/
extern void CMreturn_buffer ARGS((CManager cm, void *data));

/*!
 * try to return a buffer of incoming data.
 *
 * This call recycles a data buffer that the application has taken control
 * of through CMtake_buffer().  If it is called with a valid CM buffer, 
 * it returns 1, otherwise it returns 0.
 * \param cm The CManager in which the handler was called.
 * \param data The base address of the data (I.E. same value that was passed
 * to CMtake_buffer().
 * \return 1 if the #data value was actually from a CMtake_buffer() call.  
 * 0 otherwise. 
*/
extern int CMtry_return_buffer ARGS((CManager cm, void *data));

#include "cm_transport.h"
/*!
 * The prototype for a non-CM message handler.
 *
 * Functions matching of this prototype can be registered with
 * CMregister_non_CM_message_handler().
 * \param conn The CMConnection on which the message is available.
 * \param header The first 4 bytes of the message, encoded as an integer.
 */
typedef void (*CMNonCMHandler) ARGS((CMConnection conn,
                                     CMTransport transport,
				     char *buffer,
				     int length));

/*!
 * register a handler for raw (non-CM) messages.
 *
 * CM, like may other message layers, embeds a unique value in the first 4
 * bytes of the incoming message to identify it as a CM message.  CM
 * actually has several sets of identifying 4-byte values that it recognizes
 * as CM-internal messages.  This interface can be used to add to that set
 * to include non-CM messages (such as IIOP, HTTP, etc.).  
 * \param header The 4 bytes that identify (encoded as an integer) that
 * identify the messages to be handled.
 * \param handler The handler to be called when messages with this header
 * arrive. 
 * \note Registration is not CManager-specific, but apply to all CManagers
 * in the address space (ugly).
 * \warning Don't do this at home kids!  This API is not complete enough to
 * actually implement something like IIOP externally, but it's the thought
 * that counts.
 */
/*NOLOCK*/
extern void
CMregister_non_CM_message_handler ARGS((int header, CMNonCMHandler handler));

/*!
 * return the pointer to the static transport services structure.
 *
 * All transports share a set of basic services provided by CM, whose function
 * pointers are available through this structure.
 *
 * \return returns the pointer to the services structure.
 */
/*NOLOCK*/
extern CMtrans_services
CMget_static_trans_services ARGS(());

  /*!
   * return the pointer to a CMConnection's transport data.
   *
   * Think of this structure as the cross-product of a transport and CMConnection.
   * Transport functions use this structure to store per-connection data.
   *
   * \return returns the pointer to the transport data structure.
   */
extern void*
CMget_transport_data ARGS((CMConnection conn));

/*!
 * add a task (function) to be executed occasionally.
 *
 * \param cm The CManager to which the task is added.
 * \param func The function to be called occasionally.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 * CM poll functions are called after each round of message delivery.  I.E. 
 * once per call to CMpoll_network() if that function is used.
 */
extern void
CMadd_poll ARGS((CManager cm, CMPollFunc func, void *client_data));

/*!
 * add a task (function) to be executed with a specified periodicity.
 *
 * \param cm The CManager to which the task is added.
 * \param period_sec The number of whole seconds of the period.
 * \param period_usec The number of additional microseconds of the period.
 * \param func The function to be called periodically.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 * \return a CMTaskHandle which can be used to remove the task.
 * \note CM does not guarantee a particular periodicity, it merely applies
 * its best efforts.  I.E. It will not block wait in select() past the
 * timeout period for the next task.  However handlers may run long and I/O
 * may intervene to delay the task execution.  The task will be executed
 * when the first opportunity arises after it is scheduled.  After execution
 * is complete, the next execution will be scheduled based upon the actual
 * execution time of the current invocation (not when it was scheduled to be
 * executed). 
 */
extern CMTaskHandle
CMadd_periodic_task ARGS((CManager cm, int period_sec, int period_usec, 
			  CMPollFunc func, void *client_data));

/*!
 * add a task (function) to be executed at a later time.
 *
 * \param cm The CManager to which the task is added.
 * \param secs The number of whole seconds to delay the task.
 * \param usecs The number of additional microseconds to delay the task.
 * \param func The function to be called after the delay.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 * \return a CMTaskHandle which can be used to remove the task (only before
 * it executes).
 * \note CM does not guarantee a particular delay, it merely applies
 * its best efforts.  I.E. It will not block wait in select() past the
 * timeout period for the next task.  However handlers may run long and I/O
 * may intervene to delay the task execution.  The task will be executed
 * when the first opportunity arises after it is scheduled.  
 */
extern CMTaskHandle
CMadd_delayed_task ARGS((CManager cm, int secs, int usecs, CMPollFunc func,
			 void *client_data));

/*!
 * remove a registered periodic or delayed task.
 *
 * \param handle The handle to the task to remove.
 */
extern void
CMremove_task ARGS((CMTaskHandle handle));

/*!
 * add a task (function) to be called when the CM is shut down.
 *
 * \param cm The CManager to which a shutdown task is added.
 * \param func The function to be called upon shutdown.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 *
 * Multiple shutdown tasks can be added to the same CM and they are called
 * in the order registered.  There is currently no API for removing them.
 */
extern void
CMadd_shutdown_task ARGS((CManager cm, CMPollFunc func, void *client_data));

/*!
 * add a task to be executed with a particular periodicity.
 *
 * \param cm The CManager which should execute the task.
 * \param period The period of the task in microseconds.
 * \param func The function to be called.
 * \param client_data An uninterpreted value that is passed to the function
 * when it is called.
 * \return a CMTaskHandle which can be used to remove the task.
 * \deprecated Use CMadd_periodic_task().
 */
extern CMTaskHandle
CMadd_periodic ARGS((CManager cm, long period, CMPollFunc func,
		     void *client_data));

/*!
 * remove a registered periodic task.
 *
 * \param handle The handle to the task to remove.
 * \deprecated Use CMremove_task()
 */
extern void
CMremove_periodic ARGS((CMTaskHandle handle));

/*!
 * sleep for a given number of seconds.
 *
 * Unlike system sleep() calls, CMsleep() will continue to handle network
 * messages during the sleep time.  In particular, if CMsleep is called by
 * the network handler thread or in a single threaded program, then it will
 * enter a network handling loop until the time has elapsed.  If called by
 * other than the network handler thread in a multithread application, then
 * it will suspend on a thread condition wait until the time has elapsed.
 * \param cm The CManager upon which to sleep.
 * \param secs The number of seconds for which to sleep.
 */
extern void
CMsleep ARGS((CManager cm, int secs));

/*!
 * sleep for a given number of microseconds.
 *
 * Unlike system sleep() calls, CMusleep() will continue to handle network
 * messages during the sleep time.  In particular, if CMusleep is called by
 * the network handler thread or in a single threaded program, then it will
 * enter a network handling loop until the time has elapsed.  If called by
 * other than the network handler thread in a multithread application, then
 * it will suspend on a thread condition wait until the time has elapsed.
 * \param cm The CManager upon which to sleep.
 * \param usecs The number of microseconds for which to sleep.
 */
extern void
CMusleep ARGS((CManager cm, int usecs));

/*!
 * handle one round of network events
 *
 * \param cm The CManager for which to handle events.
 * CMpoll_network()} is one of the basic <b>network event</b> handling calls
 * in CM.  A CM network event is a basic communications occurrence, such as
 * a connection request or message arrival. The routine CMpoll_network()
 * essentially polls the network and handles some pending messages before
 * returning.  
 * \note Not all pending messages will be handled, but generally one message
 * will be handled for each connection upon which input is pending.
 */
/*NOLOCK*/
extern 
void CMpoll_network ARGS((CManager cm));

/*!
 * handle network events until shutdown.
 *
 * \param cm The CManager for which to handle events.
 * CMrun_network()} is one of the basic <b>network event</b> handling calls
 * in CM.  A CM network event is a basic communications occurrence, such as
 * a connection request or message arrival. The routine CMrun_network()
 * essentially handles network events until the CManager is shutdown.
 */
extern 
void CMrun_network ARGS((CManager cm));

typedef void (*select_func) ARGS((void *, void*));

/*NOLOCK*/
extern void
CM_fd_add_select ARGS((CManager cm, int fd, select_func handler_func,
		       void *param1, void *param2));

/*!
 * allocate a new CM condition value.
 *
 * \param cm the CManager value in which to allocate the condition.
 * \param dep the CMConnection value upon which the condition depends.
 * \return an integer value representing a CM condition.
 * \note CM condition values are used to cause a thread or program to wait
 * for a particular situation, usually for a message response to arrive.
 * In this case the condition value is acquired before sending the request
 * message, integer condition value is sent as part of the request and
 * returned in the response.  The response handler then does a
 * CMCondition_signal() as part of its operation.
 * \note The dep CMConnection value is used in error handling.  In
 * particular, if that connection dies or is closed, the condition will be
 * marked as <b>failed</b> and the corresponding CMCondition_wait() will
 * return.  Thus if the situation in which the condition is used relies upon
 * the continued operation of a connection (such as waiting for a response),
 * then that connection should be specified as the dep parameter in this
 * call.  If there is no such reliance, dep can be NULL.
 */
extern int CMCondition_get ARGS((CManager cm, CMConnection dep));

/*!
 * wait for a CM condition value.
 *
 * \param cm the CManager value in which the condition was allocated.
 * \param condition the condition upon which to wait.
 * \return 
 * - 1 if the condition was signalled normally.
 * - 0 if the CMConnection specified as dep in the CMCondition_get()
 *	        call was closed.
 * \note CM condition values are used to cause a thread or program to wait
 * for a particular situation, usually for a message response to arrive.
 * \note CMCondition_wait() is useful because it does the "right thing" in
 * both single-threaded and multi-threaded applications.  In single-threaded
 * applications it enters a network-handling loop until the condition has
 * been signaled.  In applications with a network handler thread, it checks
 * to see if it is being executed by that handler thread.  If it is *not*,
 * then it does a thread condition wait to suspect the thread.  If it is
 * being executed by the network handler thread, then it also enters a
 * network-handling loop until the condition has been signaled.
 * \warning The condition value is considered 'free'd upon return from
 * CMCondition_wait() and should not be used in any subsequent call
 * (including calls to CMCondition_get_client_data(), etc.).
 */
extern int CMCondition_wait ARGS((CManager cm, int condition));

/*!
 * signal a CM condition value.
 *
 * \param cm the CManager value in which the condition was allocated.
 * \param condition the condition to be signaled.
 * \note CM condition values are used to cause a thread or program to wait
 * for a particular situation, usually for a message response to arrive.
 * \note CMCondition_signal() notifies CM that the situation needed to
 * satisfy a particualr condition variable has occurred and any waiting
 * thread should awaken.
 */
extern void CMCondition_signal ARGS((CManager cm, int condition));

/*!
 * set the client_data associated with a condition value.
 *
 * \param cm the CManager value in which the condition is allocated.
 * \param condition the condition with which the client_data should be
 * associated. 
 * \param client_data the value to be associated with the condition.
 * \note The client_data value is not interpreted by CM, but instead
 * provides a mechanism through which information can be conveyed between
 * the requesting thread and response handler.  In a typical usage, the
 * requesting site sets the client_data to the address of storage for a
 * return value.  The response handler then uses
 * CMCondition_get_client_data() to access that address and store the return
 * value in the appropriate location.
 * \warning Calls to CMCondition_set_client_data() should occur between the
 * call to CMCondition_alloc() and CMCondition_wait().  The condition value
 * is considered 'free'd upon return from CMCondition_wait() and should not
 * be used in any subsequent call.  To avoid possible race conditions, calls
 * to CMCondition_set_client_data() should also occur before the CMwrite of
 * the request to ensure that the response doesn't arrive before the client
 * data is set.
 */
extern void CMCondition_set_client_data ARGS((CManager cm, int condition,
				       void *client_data));
/*!
 * get the client_data associated with a condition value.
 *
 * \param cm the CManager value in which the condition is allocated.
 * \param condition the condition to query for client_data.
 * \return the client_data value associated with the condition.
 * \note The client_data value is not interpreted by CM, but instead
 * provides a mechanism through which information can be conveyed between
 * the requesting thread and response handler.  In a typical usage, the
 * requesting site sets the client_data to the address of storage for a
 * return value.  The response handler then uses
 * CMCondition_get_client_data() to access that address and store the return
 * value in the appropriate location.
 * \warning Calls to CMCondition_get_client_data() should generally occur
 * in the response handler (as opposed to after CMCondition_wait()).  The
 * condition value is considered 'free'd upon return from CMCondition_wait()
 * and should not be used in any subsequent call.
 */
extern void *CMCondition_get_client_data ARGS((CManager cm, int condition));

/*!
 * test whether or not a particular condition has been signaled.
 *
 * \param cm the CManager value in which the condition is allocated.
 * \param condition the condition to test.
 * \return boolean value representing whether or not the condition has been
 * signaled. 
 * This call essentially provides a mechanism of examining the state of a
 * condition without blocking on CMCondition_wait().
 * \warning This call should not be used on a condition after
 * a CMCondition_wait() has been performed.
 */
extern int CMCondition_has_signaled ARGS((CManager cm, int condition));
/*!
 * test whether or not a particular condition has failed.
 *
 * \param cm the CManager value in which the condition is allocated.
 * \param condition the condition to test.
 * \return boolean value representing whether or not the condition has 
 * failed (I.E. its dependent connection has been closed.)
 * This call essentially provides a mechanism of examining the state of a
 * condition without blocking on CMCondition_wait().
 * \warning This call should not be used on a condition after
 * a CMCondition_wait() has been performed.
 */
extern int CMCondition_has_failed ARGS((CManager cm, int condition));

/** @defgroup malloc CM memory allocation functions
 *
 * This group of functions is used to manage CM-returned memory.
 * They are provided to handle the eventuality when CM uses its own memory
 * manager.  That hasn't happened yet, so these are identical to realloc,
 * malloc and free.
 */

/*!
 * reallocate a chunk of memory
 *
 * \param ptr the memory to reallocate
 * \param size the new size
 * \return a pointer to the new block
 */
/*NOLOCK*/
extern void* CMrealloc ARGS((void *ptr, int size));
/*!
 * allocate a chunk of memory
 *
 * \param size the requested size
 * \return a pointer to the new block
 */
/*NOLOCK*/
extern void* CMmalloc ARGS((int size));
/*!
 * free a chunk of memory
 *
 * \param ptr the memory to free
 */
/*NOLOCK*/
extern void CMfree ARGS((void *ptr));

/** @defgroup perf Performance-query functions
 * These functions intrusively test the characteristics of a connection,
 * measuring available bandwidth and current round-trip latency.
 * @{
 */
/*!
 * Probe the approximate round-trip latency on a particular connection by
 * sending a burst of data.
 *
 * This is an intrusive probe.
 * \param conn The CMConnection to be tested.
 * \param msg_size The size of message to be sent in the test.  (Latency
 * varies dramatically with the message size.)
 * \param attrs Currently this parameter is ignored, but it *should* allow
 * control over the number of messages sent.
 * \return The return value is in units of microseconds.
 * \note CM measures latency by sending a message and waiting for a
 * response.  This round-trip is called a "ping".  In the current
 * implementation, CM performs 2 ping operations to "warm up" the 
 * connection.  It then performs 5 additional ping operations, measuring the
 * time required for each.  The return value is the average of these final
 * operations. 
*/
extern long CMprobe_latency ARGS((CMConnection conn, int msg_size,
				  attr_list attrs));

/*!
 * Probe the available bandwidth on a particular connection by sending a
 * burst of data.
 *
 * This is an intrusive probe.
 * \param conn The CMConnection to be tested.
 * \param size The size of message to be sent in the test.  (Bandwidth
 * varies dramatically with the message size.)
 * \param attrs Currently this parameter is ignored, but it *should* allow
 * control over the number of messages sent.
 * \return The return value is in units of KBytes per second.  
 * \note In the current implementation, CM sends \f$N\f$ messages to probe
 * available bandwidth, where \f$N\f$ is calculated as \f$100000/size\f$.
 * That is, CMprobe_bandwidth sends about 100Kbytes of data.
*/
extern long
CMprobe_bandwidth ARGS((CMConnection conn, int size, attr_list attrs));

/*!
 * Probe the available bandwidth on a particular connection by sending several streams
 * and do a linear regression.
 *
 * This is an intrusive probe.
 * \param conn The CMConnection to be tested.
 * \param size The size of message to be sent in the test.  (Bandwidth
 * varies dramatically with the message size.)
 * \param attrs Currently this parameter is ignored, but it *should* allow
 * control over the number of messages sent.
 * \return The return value is in units of KBytes per second.  
 * \note In the current implementation, CM sends \f$N\f$ messages to probe
 * available bandwidth, where \f$N\f$ is calculated as \f$100000/size\f$.
 * That is, CMprobe_bandwidth sends about 100Kbytes of data.
*/
extern double
CMregressive_probe_bandwidth ARGS((CMConnection conn, int size, attr_list attrs));

/*@}*/
/*!
 * Try to return the IP address of the current host as an integer.
 */
/*NOLOCK*/
extern int
CMget_self_ip_addr();
/** @defgroup attrs Attributes used in various portions of CM
 * @{
 */
/** @defgroup sockattr Sockets attributes
 * @{
 */
/*! "CONNECTION_FILE_DESCRIPTOR" */
#define CM_FD ATL_CHAR_CONS('C','S','F','D')

/*! "THIS_CONN_PORT" */
#define CM_THIS_CONN_PORT ATL_CHAR_CONS('C','S','C','P')

/*! "PEER_CONN_PORT" */
#define CM_PEER_CONN_PORT ATL_CHAR_CONS('C','S','P','P')

/*! "PEER_IP" */
#define CM_PEER_IP ATL_CHAR_CONS('C','P','I','P')

/*! "PEER_LISTEN_PORT" */
#define CM_PEER_LISTEN_PORT ATL_CHAR_CONS('C','S','P','L')

/*! "PEER_HOSTNAME" */
#define CM_PEER_HOSTNAME ATL_CHAR_CONS('C','P','H','O')

/*! "IP_HOST" */
#define CM_IP_HOSTNAME ATL_CHAR_CONS('C','I','P','H')

/*! "IP_ADDR" */
#define CM_IP_ADDR ATL_CHAR_CONS('C','I','P','A')

/*! "IP_PORT" */
#define CM_IP_PORT ATL_CHAR_CONS('C','I','P','P')

/*! "CONN_BLOCKING" */
#define CM_CONN_BLOCKING ATL_CHAR_CONS('C','n','B','l')
/*! @}*/

/*! "UDP_PORT" */
#define CM_UDP_PORT ATL_CHAR_CONS('C','U','P','P')

/*! "CM_TRANSPORT */
#define CM_TRANSPORT ATL_CHAR_CONS('C','T','r','a')

/*! "CM_NETWORK_POSTSCRIPT */
#define CM_NETWORK_POSTFIX ATL_CHAR_CONS('C','N','P','f')

/*! "ATM_ADDRESS" */
#define CM_ATM_ADDRESS ATL_CHAR_CONS('C','A','T','A')

/*! "ATM_SELECTOR" */
#define CM_ATM_SELECTOR ATL_CHAR_CONS('C','A','T','S')

/*! "ATM_BHLI" */
#define CM_ATM_BHLI ATL_CHAR_CONS('C','A','T','B')

/*! "QOS_CLASS" */
#define CM_ATM_QOS_CLASS ATL_CHAR_CONS('C','A','Q','C')

/*! "QOS_PCR" */
#define CM_ATM_QOS_PCR ATL_CHAR_CONS('C','A','Q','P')

/*! "QOS_SCR" */
#define CM_ATM_QOS_SCR ATL_CHAR_CONS('C','A','Q','S')
/*! "QOS_MBS" */

#define CM_ATM_QOS_MBS ATL_CHAR_CONS('C','A','Q','M')
/*! "CONN_FD" */

#define CM_ATM_FD ATL_CHAR_CONS('C','A','F','D')

/*! "PEER_ATM_ADDRESS" */
#define CM_ATM_REMOTE_ADDRESS ATL_CHAR_CONS('C','A','R','A')

/*! "PEER_ATM_SELECTOR" */
#define CM_ATM_REMOTE_SELECTOR ATL_CHAR_CONS('C','A','R','S')

/*! "PEER_ATM_BHLI" */
#define CM_ATM_REMOTE_BHLI ATL_CHAR_CONS('C','A','R','B')

/*! "CONN_VPI" */
#define CM_ATM_CONN_VPI ATL_CHAR_CONS('C','A','C','V')

/*! "CONN_VCI" */
#define CM_ATM_CONN_VCI ATL_CHAR_CONS('C','A','C','v')

/*! "MTP_HOST" */
#define CM_MTP_HOSTNAME ATL_CHAR_CONS('C','M','H','o')

/*! "MTP_ADDR" */
#define CM_MTP_ADDR ATL_CHAR_CONS('C','M','A','d')

/*! "MTP_PORT" */
#define CM_MTP_PORT ATL_CHAR_CONS('C','M','P','o')

/*! "CM_RECV_ERR" */
#define CM_RECV_ERR ATL_CHAR_CONS('C','R','R','E')

/*! "CM_SEND_ERR" */
#define CM_SEND_ERR ATL_CHAR_CONS('C','R','S','E')

/*! "CM_RECV_RATE" */
#define CM_RECV_RATE ATL_CHAR_CONS('C','R','R','R')

/*! "CM_SEND_RATE" */
#define CM_SEND_RATE ATL_CHAR_CONS('C','R','S','R')

/*! "CM_RTT" */
#define CM_RTT ATL_CHAR_CONS('C','R','R', 't')


/*! "CM_PROG_RATE" */
#define CM_PROG_RATE ATL_CHAR_CONS('C','R','P','R')

/*! "CM_QOS" */
#define CM_QOS ATL_CHAR_CONS('C','Q','O','S')

/*! "CM_MAX_SEG" */
#define CM_MAX_SEG ATL_CHAR_CONS('C','R','M','S')

/*! "CM_MARK" */
#define CM_MARK ATL_CHAR_CONS('C','R','M','a')

/*! "CM_START_ADAPT" */
#define CM_START_ADAPT ATL_CHAR_CONS('C','R','S','A')

/*! "CM_ADAPT_DEGREE" */
#define CM_ADAPT_DEGREE ATL_CHAR_CONS('C','R','A','D')

/*! "CM_ADAPT_COND" */
#define CM_ADAPT_COND ATL_CHAR_CONS('C','R','A','C')

/*! "CM_REBWM_RLEN" */
#define CM_REBWM_RLEN ATL_CHAR_CONS('C','R', 'B', 'L')

/*! "CM_REBWM_REPT" */
#define CM_REBWM_REPT ATL_CHAR_CONS('C','R', 'B', 'R')

/*! "CM_BW_MEASURE_SIZE" */
#define CM_BW_MEASURE_SIZE ATL_CHAR_CONS('C','B', 'M', 'S')

/*! "CM_BW_MEASURE_SIZEINC" */
#define CM_BW_MEASURE_SIZEINC ATL_CHAR_CONS('C','B', 'M', 'N')

/*! "CM_BW_MEASURE_INTERVAL" */
#define CM_BW_MEASURE_INTERVAL ATL_CHAR_CONS('C','B', 'M', 'I')

/*! "CM_BW_MEASURE_TASK" */
#define CM_BW_MEASURE_TASK ATL_CHAR_CONS('C','B', 'M', 'T')

/*! "CM_BW_MEASURED_VALUE" */
#define CM_BW_MEASURED_VALUE ATL_CHAR_CONS('C','B', 'M', 'V')

/*! "CM_BW_MEASURED_COF" */
#define CM_BW_MEASURED_COF ATL_CHAR_CONS('C','B', 'M', 'C')


/* @}*/

/** @defgroup mcastattr Multicast attributes
 * @{
 */
/*! "MCAST_ADDR" */
#define CM_MCAST_ADDR ATL_CHAR_CONS('C','M','C','A')

/*! "MCAST_PORT" */
#define CM_MCAST_PORT ATL_CHAR_CONS('C','M','C','P')
/* @}*/

/** @defgroup udpattr UDP attributes
 * @{
 */
/*! "UDP_ADDR" */
#define CM_UDP_ADDR ATL_CHAR_CONS('C','U','U','A')

/* @}*/

/** @defgroup rudpattr RUDP attributes
 * @{
 */
/*! "CM_FREEZE_WND" */
#define CM_FREEZE_WND ATL_CHAR_CONS('C', 'R', 'F', 'W') 

/*! "CM_MEASURE" */
#define CM_MEASURE ATL_CHAR_CONS('C', 'R', 'M', 'e')

/*! "CM_BW" */
#define CM_BW ATL_CHAR_CONS('C', 'R', 'B', 'W')

/*! "CM_SNDWND" */
#define CM_SNDWND ATL_CHAR_CONS('C', 'R', 'S', 'W')

/*! "CM_EVENT_SIZE" */
#define CM_EVENT_SIZE ATL_CHAR_CONS('C', 'E', 'T', 'S')

/*! "EV_EVENT_LSUM" */
#define EV_EVENT_LSUM ATL_CHAR_CONS('E', 'E', 'L', 'S')

/*! "CM_PATHRATE" */
#define CM_PATHRATE ATL_CHAR_CONS('P','T','R', 'T')
/* @}*/

/** @defgroup evpath EVPath functions and types
 * @{
 */
struct _EVStone;
struct _EVSource;
/*!
 * EVStone a stone is an elementary building block of paths
 *
 * EVStone is an integer-typed opaque handle.  Its only external use is 
 * to act as an external stone identifier for remote operations (such as 
 * specifying the remote target stone in an output action)
 */
typedef int EVstone;
/*!
 * EVaction actions, associated with stones, are the mechanisms through 
 * which data flow operations are defined.
 *
 * EVaction is an opaque integer-typed handle.  An EVaction handle is 
 * interpreted in the context of the stone it is associated with and is 
 * not unique across stones.
 */
typedef int EVaction;
/*!
 * EVsource an EVsource is a source handle used to submit events to EVpath.
 * An EVsource specifies both the (local) target stone and the format 
 * (fully-specified structured data type) of the data that will be submitted 
 * using this handle.  
 *
 * EVsource is an opaque handle.
 */
typedef struct _EVSource *EVsource;

/*!
 * The prototype for an EVPath terminal handler function.
 *
 * EVPath allows application-routines matching this prototype to be 
 * registered as sinks on stones.
 * \param cm The CManager with which this handler was registered.
 * \param message A pointer to the incoming data, cast to void*.  The real
 * data is formatted to match the fields of with which the format was
 * registered. 
 * \param client_data This value is the same client_data value that was
 * supplied in the EVassoc_terminal_action() call.  It is not interpreted by CM,
 * but instead can be used to maintain some application context.
 * \param attrs The attributes (set of name/value pairs) that this message
 * was delivered with.  These are determined by the transport and may
 * include those specified in CMwrite_attr() when the data was written.
 */
typedef int (*EVSimpleHandlerFunc) ARGS((CManager cm, 
					  void *message, void *client_data,
					  attr_list attrs));
struct _event_item;

/*!
 * Allocate a stone.
 *
 * Stones are the basic abstraction of EVPath, the entity to which events
 * are submitted and with which actions are associated.  The value returned
 * from EValloc_stone() is actually a simple integer which may be transmitted
 * to remote locations (for example for use in remote output actions).
 * \param cm The CManager which will manage the control for this stone.
 * \return The stone identifier, an integer EVstone value, which can be used
 * in subsequent calls to associate actions with the stone.
 */
/*REMOTE*/
extern EVstone
EValloc_stone(CManager cm);

/*!
 * Free a stone.
 *
 * This call also free's all actions and data associated with a stone, 
 * including enqueued events if any.
 * \param cm The CManager from which this stone was allocated.
 * \param stone The stone to free.
 */
/*REMOTE*/
extern void
EVfree_stone(CManager cm, EVstone stone);

/*!
 * Associate a terminal action (sink) with a stone.
 *
 * The specified handler will be called when data matching the 
 * format_list arrives at the stone.  The event data supplied may not 
 * remain valid after the handler call returns.  EVtake_event_buffer() may 
 * be used to ensure longer-term validity of the event data.  The 
 * parameters to the handler are those of EVSimpleHandlerFunc.
 * \param cm The CManager from which this stone was allocated.
 * \param stone The stone to which to register the action.
 * \param format_list The list of formats which describe the event data 
 * structure that the function accepts.
 * \param handler The handler function that will be called with data arrives.
 * \param client_data An uninterpreted value that is passed to the hanlder
 * function when it is called.
 * \return An action identifier, an integer EVaction value, which can be used
 * in subsequent calls to modify or remove the action.
 */
/*REMOTE*/
extern EVaction
EVassoc_terminal_action(CManager cm, EVstone stone, CMFormatList format_list, 
			EVSimpleHandlerFunc handler, void* client_data);

/*!
 * Associate a terminal action (sink) with a new stone.
 *
 * The specified handler will be called when data matching the 
 * format_list arrives at the stone.  The event data supplied may not 
 * remain valid after the handler call returns.  EVtake_event_buffer() may 
 * be used to ensure longer-term validity of the event data.  The 
 * parameters to the handler are those of EVSimpleHandlerFunc.  This 
 * function differs from the previous function only in that it creates
 * a stone rather than using an existing stone.
 * \param cm The CManager from which this stone was allocated.
 * \param format_list The list of formats which describe the event data 
 * structure that the function accepts.
 * \param handler The handler function that will be called with data arrives.
 * \param client_data An uninterpreted value that is passed to the hanlder
 * function when it is called.
 * \return The stone identifier, an integer EVstone value, which can be used
 * in subsequent calls.
 */
/*REMOTE*/
extern EVstone
EVcreate_terminal_action(CManager cm, CMFormatList format_list, 
			EVSimpleHandlerFunc handler, void* client_data);

/*extern EVaction
EVassoc_queued_action(CManager cm, EVstone stone, char *queue_spec, 
void *client_data);*/

/*!
 * Associate an immediate non-terminal action with a stone.
 *
 * EVassoc_immediate_action() can be used to install handlers which
 * take only a single event as input and can therefore run and "consume"
 * their data immediately.  In particular, they are distinct from actions
 * which may leave their input data enqueued for some time (typically
 * handlers which might require more than one event to act).  The current
 * EVPath implementation supports only immediate actions with one input and
 * one output, but multiple output actions will be implemented soon.  
 * \param cm The CManager from which this stone was allocated.
 * \param stone The stone to which to register the action.
 * \param action_spec An action specification of the sort created by
 * create_filter_action_spec() or create_transform_action_spec().
 * \param client_data An uninterpreted value that is passed to the handler
 * function when it is called.
 * \return An action identifier, an integer EVaction value, which can be used
 * in subsequent calls to modify or remove the action.
 */
/*REMOTE*/
extern EVaction
EVassoc_immediate_action(CManager cm, EVstone stone, char *action_spec, 
		      void *client_data);

/*!
 * Associate an immediate non-terminal action with a new stone.
 *
 * EVassoc_immediate_action() can be used to install handlers which
 * take only a single event as input and can therefore run and "consume"
 * their data immediately.  In particular, they are distinct from actions
 * which may leave their input data enqueued for some time (typically
 * handlers which might require more than one event to act).  The current
 * EVPath implementation supports only immediate actions with one input and
 * one output, but multiple output actions will be implemented soon.  This 
 * function differs from the previous function only in that it creates
 * a stone rather than using an existing stone.
 * \param cm The CManager from which this stone was allocated.
 * \param action_spec An action specification of the sort created by
 * create_filter_action_spec() or create_transform_action_spec().
 * \param target_list A -1 terminated list of stones to which outgoing
 * data is to be sent.  This initial list can be NULL (or merely have
 * an initial 0) to specify no targets at action initialization time.  
 * Values are filled in later with EVaction_set_output().
 * \return The stone identifier, an integer EVstone value, which can be used
 * in subsequent calls.
 */
/*REMOTE*/
extern EVstone
EVcreate_immediate_action(CManager cm, char *action_spec, EVstone *target_list);

/*!
 * Direct the output of a stone action to another local target stone
 *
 * Immediate and queued actions have one or more outputs from which data
 * will emerge.  EVaction_set_output() is used to assigne each of these
 * outputs to a local stone.  (It is NOT used with output stones.)
 * \param cm The CManager from which this stone was allocated.
 * \param stone The stone to which the action is registered.
 * \param action The action whose output is to be assigned.
 * \param output_index The zero-based index of the output to assign.
 * \param target_stone The stone to which the specified output should be
 * directed. 
 * \return An action identifier, an integer EVaction value, which can be used
 * in subsequent calls to modify or remove the action.
 */
/*REMOTE*/
extern int
EVaction_set_output(CManager cm, EVstone stone, EVaction action, 
		    int output_index, EVstone target_stone);

/*!
 * Associate an immediate non-ECL filter action with a stone.
 *
 * EVassoc_filter_action() is similar to EVassoc_immediate_action() called
 * with an action spec generated by create_filter_action_spec(), except that
 * a function pointer is provided directly instead of having the function
 * generated by ECL.
 * 
 * \param cm The CManager from which this stone was allocated.
 * \param stone The stone to which to register the action.
 * \param incoming_format_list The list of formats which describe the event data 
 * structure that the function accepts.
 * \param handler The handler function that will be called with data arrives.
 * \param out_stone The local stone to which output should be directed.
 * \param client_data An uninterpreted value that is passed to the hanlder
 * function when it is called.
 * \return An action identifier, an integer EVaction value, which can be used
 * in subsequent calls to modify or remove the action.
 *
 * \deprecated  This function needs to go away and instead the functionality
 * should be integrated into a new create_*_action_spec() call that would
 * then be passed to EVassoc_immediate_action().
 */
extern EVaction
EVassoc_filter_action(CManager cm, EVstone stone, 
		      CMFormatList incoming_format_list, 
		      EVSimpleHandlerFunc handler, EVstone out_stone,
		      void* client_data);

/*!
 * Associate an output action with a stone.
 *
 * Output actions perform network data transmission between address spaces.
 * EVassoc_output_action will acquire a CM-level connection to the remote
 * process specified by the \b contact_list parameter.  Data delivered to
 * the local stone specified by \b stone will be encoded, sent over the 
 * network link and delivered to \b remote_stone in the target address space.
 *
 * \param cm The CManager from which this stone was allocated.
 * \param stone The local stone to which to register the action.
 * \param contact_list A CM-level contact list (such as from
 * CMget_contact_list()) specifying the remote address space to connect to. 
 * \param remote_stone The stone ID in the remote address space to which
 * data is to be delivered.
 * \return An action identifier, an integer EVaction value, which can be used
 * in subsequent calls to modify or remove the action.
 *
 * Output actions are associated with the default action of a stone and are
 * non-specific as far as input data, encoding and transmitting any event
 * presented to the action.  Output actions may not be modified after
 * association. 
 */
/*REMOTE*/
extern EVaction
EVassoc_output_action(CManager cm, EVstone stone, attr_list contact_list, 
		      EVstone remote_stone);

/*!
 * Associate an output action with a new stone.
 *
 * Output actions perform network data transmission between address spaces.
 * EVassoc_output_action will acquire a CM-level connection to the remote
 * process specified by the \b contact_list parameter.  Data delivered to
 * the local stone specified by \b stone will be encoded, sent over the 
 * network link and delivered to \b remote_stone in the target address space.
 * This function differs from the previous function only in that it creates
 * a stone rather than using an existing stone.
 *
 * \param cm The CManager from which this stone was allocated.
 * \param contact_list A CM-level contact list (such as from
 * CMget_contact_list()) specifying the remote address space to connect to. 
 * \param remote_stone The stone ID in the remote address space to which
 * data is to be delivered.
 * \return The stone identifier, an integer EVstone value, which can be used
 * in subsequent calls.
 *
 * Output actions are associated with the default action of a stone and are
 * non-specific as far as input data, encoding and transmitting any event
 * presented to the action.  Output actions may not be modified after
 * association. 
 */
/*REMOTE*/
extern EVstone
EVcreate_output_action(CManager cm, attr_list contact_list, 
		      EVstone remote_stone);

/*!
 * Associate a split action with a stone.
 *
 * Split actions replicate an incoming event to multiple output target
 * stones.  All output paths receive every incoming event. (Reference counts
 * are updated, the event is not actually copied.)  Split actions may be
 * modified after association by using EVaction_add/remote_split_target() to
 * modify the target list.
 *
 * \param cm The CManager from which this stone was allocated.
 * \param stone The local stone to which to register the action.
 * \param target_list A '-1' terminated list of stones to which incoming
 * data is to be replicated.  This initial list can be NULL (or merely have
 * an initial '-1') to specify no targets at action initialization time.
 * \return An action identifier, an integer EVaction value, which can be used
 * in subsequent calls to modify or remove the action.
 */
/*REMOTE*/
extern EVaction
EVassoc_split_action(CManager cm, EVstone stone, EVstone *target_list);

/*!
 * Associate a split action with a new stone.
 *
 * Split actions replicate an incoming event to multiple output target
 * stones.  All output paths receive every incoming event. (Reference counts
 * are updated, the event is not actually copied.)  Split actions may be
 * modified after association by using EVaction_add/remote_split_target() to
 * modify the target list.  This function differs from the previous function 
 * only in that it creates a stone rather than using an existing stone.
 *
 * \param cm The CManager from which this stone was allocated.
 * \param target_list A '-1' terminated list of stones to which incoming
 * data is to be replicated.  This initial list can be NULL (or merely have
 * an initial -1) to specify no targets at action initialization time.
 * \return The stone identifier, an integer EVstone value, which can be used
 * in subsequent calls.
 */
/*REMOTE*/
extern EVstone
EVcreate_split_action(CManager cm, EVstone *target_list);

/*!
 * Add a target to a split action.
 *
 * This call adds a new target stone to the list of stones to which a split
 * action will replicate data.
 *
 * \param cm The CManager from which this stone was allocated.
 * \param stone The split stone.
 * \param action The split action ID (as returned by EVassoc_split_action()).
 * \param target_stone The target stone to add to the list.
 * \return Returns 1 on success, 0 on failure (fails if there is not a split
 * action on the specified stone).
 */
/*REMOTE*/
extern int
EVaction_add_split_target(CManager cm, EVstone stone, EVaction action,
			  EVstone target_stone);

/*!
 * Remove a target from a split action.
 *
 * This call removes a target stone from the list of stones to which a split
 * action will replicate data.
 *
 * \param cm The CManager from which this stone was allocated.
 * \param stone The split stone.
 * \param action The split action ID (as returned by EVassoc_split_action()).
 * \param target_stone The target stone to remove from the list.
 */
/*REMOTE*/
extern void
EVaction_remove_split_target(CManager cm, EVstone stone, EVaction action,
			  EVstone target_stone);

/*!
 * Create a submission handle (EVsource).
 *
 * EVpath is optimized for repetitive event streams.  Rather than specifying
 * the characteristics of data and the stone to which it is to be submitted
 * on every event submission, we use associate those characteristics with
 * EVsource handles.  These handles serve as a cache for internal information.
 *
 * \param cm The CManager associated with the stone.
 * \param stone The stone to which data is to be submitted.
 * \param data_format The CMFormatList describing the representation of the
 * data. 
 * \return An EVsource handle for use in later EVsubmit() calls.
 */
extern EVsource
EVcreate_submit_handle(CManager cm, EVstone stone, CMFormatList data_format);

/*!
 * Free a source.
 *
 * This call free's the resources associated with an EVsource handle..
 * \param source  The source to free.
 */
extern void
EVfree_source(EVsource source);

/*!
 * The prototype for a function which will free the memory associated with
 * an event.
 *
 * Normally, the EVpath event submission functions do not return until
 * it is safe for the application to destroy the submitted data (I.E. until
 * EVpath is finished with it).  However, if a "free" function is associated
 * with the event through the EVsource, EVpath will return sooner if there
 * is another thread of control available to prosecute the actions on the
 * event.  EVpath will then call the application-supplied free function to
 * free the event when the event data is no longer required.
 * Application-supplied event free functions must satisfy this profile. 
 * \param event_data  The address of the event data, expressed as a void*.
 * \param client_data The parameter is used to supply the free function with
 * the same client_data value that was specified in the
 * EVcreate_submit_handle_free() call.
 */
typedef void (*EVFreeFunction) ARGS((void *event_data, void *client_data));

/*!
 * Create a submission handle (EVsource), specifying a free function for the
 * event. 
 *
 * EVpath is optimized for repetitive event streams.  Rather than specifying
 * the characteristics of data and the stone to which it is to be submitted
 * on every event submission, we use associate those characteristics with
 * EVsource handles.  These handles serve as a cache for internal information.
 * This version of the call allows an EVFreeFunction to be associated with
 * the handle.  EVpath will take ownership of the submitted data, calling
 * the free function when processing is finished.  
 *
 * \param cm The CManager associated with the stone.
 * \param stone The stone to which data is to be submitted.
 * \param data_format The CMFormatList describing the representation of the
 * data. 
 * \param free_func  The EVFreeFunction to call when EVPath has finished
 * processing the submitted data.
 * \param client_data The parameter is supplied to the free function and can
 * be used to supply it with additional information.
 * \return An EVsource handle for use in later EVsubmit() calls.
 */
extern EVsource
EVcreate_submit_handle_free(CManager cm, EVstone stone, CMFormatList data_format,
			    EVFreeFunction free_func, void *client_data);

/*!
 * Submit an event for processing by EVPath.
 *
 * EVsubmit submits an event for processing by EVPath.  The format of the
 * submitted data must match the description given by the \b data_format
 * parameter when the EVsource handle was created.  The \b attrs parameter
 * specifies the attributes (name/value pairs) that the event is submitted
 * with.  These attributes will be delivered to the final terminal, as well
 * as being available at intermediate processing points.  Some attributes
 * may affect the processing or transmission of data, depending upon the
 * specific transport or processing agents.
 * \param source The EVsource handle through which data is to be submitted.
 * \param data The data to be submitted, represented as a void*.
 * \param attrs The attribute list to be submitted with the data.
 */
extern void
EVsubmit(EVsource source, void *data, attr_list attrs);

/*!
 * Submit an event for processing by EVPath.
 *
 * EVsubmit submits an event for processing by EVPath.  The format of the
 * submitted data must match the description given by the \b data_format
 * parameter when the EVsource handle was created.  The \b attrs parameter
 * specifies the attributes (name/value pairs) that the event is submitted
 * with.  These attributes will be delivered to the final terminal, as well
 * as being available at intermediate processing points.  Some attributes
 * may affect the processing or transmission of data, depending upon the
 * specific transport or processing agents.
 * \param source The EVsource handle through which data is to be submitted.
 * \param data The data to be submitted, represented as a void*.
 * \param free_func  The EVFreeFunction to call when EVPath has finished
 * processing the submitted data.
 * \param attrs The attribute list to be submitted with the data.
 *
 * \deprecated  This function is used to underly ECho, which allows the free
 * function to be specified with the submit.  New applications should
 * specify the free function in the submit handle.
 */
extern void
EVsubmit_general(EVsource source, void *data, EVFreeFunction free_func,
		 attr_list attrs);

/*!
 * Submit a pre-encoded event for processing by EVPath.
 *
 * EVsubmit submits a pre-encoded event for processing by EVPath.  The event 
 * must be a contiguous PBIO-encoded block of data.  The \b attrs parameter
 * specifies the attributes (name/value pairs) that the event is submitted
 * with.  These attributes will be delivered to the final terminal, as well
 * as being available at intermediate processing points.  Some attributes
 * may affect the processing or transmission of data, depending upon the
 * specific transport or processing agents.
 * \param cm The CManager associated with the stone.
 * \param stone The stone to which data is to be submitted.
 * \param data The pre-encoded data to be submitted, represented as a void*.
 * \param data_len The length of the pre-encoded data block.
 * \param attrs The attribute list to be submitted with the data.
 *
 */
extern void
EVsubmit_encoded(CManager cm, EVstone stone, void *data, int data_len,
		 attr_list attrs);

/*!
 * Assume control over a incoming buffer of data.
 *
 * This call is designed to be used inside a EVSimpleHandlerFunc.  Normally
 * data buffers are recycled and EVPath only guarantees that the data
 * data delivered to an EVSimpleHandlerFunc will be valid for the duration
 * data of the call.  In that circumstance, a handler that wanted to
 * data preserve the data for longer than its own duration (to pass it to a
 * data thread or enter it into some other data structure for example) would
 * data have to copy the data.  To avoid that inefficiency, 
 * EVtake_event_buffer() allows the handler to take control of the 
 * buffer holding its incoming data.  The buffer will then not be recycled
 * until it is returned to CM with EVreturn_event_buffer().
 * \param cm The CManager in which the handler was called.
 * \param event The base address of the data (I.E. the message parameter to
 * the EVSimpleHandlerFunc).
 * \return 0 on error, 1 on success;
*/
extern int
EVtake_event_buffer ARGS((CManager cm, void *event));

/*!
 * Return a buffer of incoming data.
 *
 * This call recycles a data buffer that the application has taken control
 * of through EVtake_event_buffer().
 * \param cm The CManager in which the handler was called.
 * \param event The base address of the data (I.E. same value that was passed
 * to EVtake_event_buffer().
*/
extern void
EVreturn_event_buffer ARGS((CManager cm, void *event));

/*!
 * return the IOFormat associated with an EVsource handle.
 *
 * Some middleware may find it useful to access the IOFormat that is
 * produced when the CMFormatList associated with a source is registered
 * with PBIO.  This call merely gives access to that information to save a
 * reregistration step.
 * \param source The EVsource value for which to retrieve the associated
 * IOFormat.
 */
extern IOFormat
EVget_src_ref_format(EVsource source);

/*!
 * Enable periodic auto-submits of NULL events on a stone.
 *
 * \param cm The CManager in which the stone is registered.
 * \param stone_num The stone which should receive auto-submits.
 * \param period_sec The period at which submits should occur, seconds portion.
 * \param period_usec The period at which submits should occur, microseconds
 * portion.
 */
/*REMOTE*/
extern void
EVenable_auto_stone(CManager cm, EVstone stone_num, int period_sec, 
		    int period_usec);

/*!
 * Enable periodic auto-submits of NULL events on a stone. This 
 * function differs from the previous function only in that it creates
 * a stone rather than using an existing stone.
 *
 * \param cm The CManager in which the stone is registered.
 * \param period_sec The period at which submits should occur, seconds portion.
 * \param period_usec The period at which submits should occur, microseconds
 * portion.
 * \param action_spec An action specification of the sort created by
 * create_filter_action_spec() or create_transform_action_spec().
 * \param out_stone The local stone to which output should be directed.
 * \return The stone identifier, an integer EVstone value, which can be used
 * in subsequent calls.
 */
/*REMOTE*/
extern EVstone
EVcreate_auto_stone(CManager cm, int period_sec, int period_usec, 
		    char *action_spec, EVstone out_stone);


/*!
 * Cause a stone to suspend operation
 *
 * This function causes a stone to enter a "suspended" state in which
 * incoming data will simply be queued, rather than submitted to any actions
 * which might be registered.  In the case of an output stone, will allow
 * the stone to finish the output action it is currently executing and then
 * prevent the output stone from sending any more data to the target stone.
 * \param cm The CManager in which the stone is registered
 * \param stone_id The stone which is to be frozen
 * \return Returns 1 on success, 0 on failure
 */ 
/*REMOTE*/
extern int
EVfreeze_stone(CManager cm, EVstone stone_id);

/*!
 * Cause a stone to resume operation
 *
 * This function causes a frozen stone (via EVfreeze_stone()) to resume
 * operation.  Pending data will be submitted to actions during the next
 * action processing phase. 
 * \param cm The CManager in which the stone is registered
 * \param stone_id The stone to unfreeze
 * \return Returns 1 on success, 0 on failure
 */ 
/*REMOTE*/
extern int
EVunfreeze_stone(CManager cm, EVstone stone_id);

/*!
 * Drain a stone
 *
 * This function is a blocking call that suspends the caller until all
 * events queued on a stone are processed (if processing is possible, it
 * might not be for events that require the presence of other events).
 * The function is typically used after upstream stones have been frozen
 * with EVfreeze_stone() during a reconfiguration action.  EVdrain_stone()
 * then makes sure a stone is as empty as possible prior to event extraction
 * and destruction.
 * \param cm The CManager in which the stone is registered
 * \param stone_id The stone which is to be drained
 * \return Returns 1 on success, 0 on failure
 */
/*REMOTE*/
extern int
EVdrain_stone(CManager cm, EVstone stone_id);

/*!
 * Return the queued events associated with a stone and its actions.
 * 
 * This function will be called by EVdrain_stone. It will form an array of
 * structures where each structure will contain the size of the encoded 
 * event and a pointer to the encoded event. The array will contain an entry 
 * for each event, associated with the stone or its actions.   
 * \param cm The CManager in which the stone is registered
 * \param stone_id The stone whose associated events are to be extracted
 * \return  Returns an array of structures (EVevent_list) containing the
 * lengths of events and pointers to the encoded versions of events
 */
/*REMOTE*/
extern EVevent_list
EVextract_stone_events(CManager cm, EVstone stone_id);

/*!
 * Return the attribute list associated with a stone.
 *
 * This function will be called by EVdrain_stone. It will return the atrributes of 
 * the stone.
 * \param cm The CManager in which the stone is registered
 * \param stone_id The stone whose attributes are to be extracted
 * \return attr_list Returns the attribute list associated with the stone
 */
/*REMOTE*/
extern attr_list
EVextract_attr_list(CManager cm, EVstone stone_id);

/*!
 * Free a stone after it has been drained.
 *
 * This function will wait till a stone is drained. Then it will free all the
 * data and events associated with the stone.
 * \param cm The CManager in which the stone is registered
 * \param stone_id The stone which is to be destroyed
 * \return Returns 1 on success, 0 on failure
 */ 
/*REMOTE*/
extern int
EVdestroy_stone(CManager cm, EVstone stone_id);

/*!
 * create an action specification for a filter function.
 *
 * 
 * \param format_list A description of the incoming event data that the
 * filter expects. 
 * \param function The filter function itself.  A zero return value means
 * that the data should be discarded. 
 */
/*NOLOCK*/
extern char *
create_filter_action_spec(CMFormatList format_list, char *function);

/*!
 * create an action specification for a router function.
 *
 * 
 * \param format_list A description of the incoming event data that the
 * router function expects. 
 * \param function The router function itself.  A negative return value means
 * that the data should be discarded.  A positive value less than the number
 * of output values that have been set with EVaction_set_output() indicates
 * which of the output paths the input data should be submitted to.  Return
 * values larger than the number of output paths have undefined behaviour.
 */
/*NOLOCK*/
extern char *
create_router_action_spec(CMFormatList format_list, char *function);

/*!
 * create an action specification that transforms event data.
 *
 * \param format_list A description of the incoming event data that the
 * transformation expects. 
 * \param out_format_list A description of the outgoing event data that the
 * transformation will produce. 
 * \param function The processing that will perform the transformation.  A
 * zero return value means that the output data should be ignored/discarded.
 */
/*NOLOCK*/
extern char *
create_transform_action_spec(CMFormatList format_list, CMFormatList out_format_list, char *function);

/*!
 * create an action specification that operates on multiple queues of events
 *
 * \param input_format_lists A null-terminated list of null-terminated lists
 *  of descriptions of  the incoming event data types that the transformation
 *  expects. 
 * \param function The processing that will perform the transformation.  A
 * zero return value means that the output data should be ignored/discarded.
 */
/*NOLOCK*/
extern char *
create_multiqueued_action_spec(CMFormatList *input_format_lists, char *function);

/*!
 * Print a description of stone status to standard output.
 *
 * A simple dump function that can be used for debugging.
 * \param cm The CManager to which the stone is registered.
 * \param stone_num  The stone to dump.
 */
void
EVdump_stone(CManager cm,  EVstone stone_num);

/*!
 * The prototype of a specific immediate handler funcion.
 *
 * This function prototype is used by the EVPath internal "response"
 * interface.  At some point, the response interface will likely become
 * external so that EVPath's response to unknown data can be customized.
 * However, at the moment this is an internal interface.
 */
typedef int (*EVImmediateHandlerFunc) ARGS((CManager cm, 
					    struct _event_item *event, 
					    void *client_data,
					    attr_list attrs, 
					    int out_count,
					    int *out_stones));
/*!
 * Associate a specific (mutated) immediate handler funcion.
 *
 * This function is used by the EVPath internal "response" interface.  At
 * some point, the response interface will likely become external so that
 * EVPath's response to unknown data can be customized.  However, at the
 * moment this is an internal interface.
 */
extern EVaction
EVassoc_mutated_imm_action(CManager cm, EVstone stone, EVaction act_num,
			   EVImmediateHandlerFunc func, void *client_data,
			   IOFormat reference_format);

/*!
 * Associate a conversion action.
 *
 * This function is used by the EVPath internal "response" interface.  At
 * some point, the response interface will likely become external so that
 * EVPath's response to unknown data can be customized.  However, at the
 * moment this is an internal interface.
 */
extern void
EVassoc_conversion_action(CManager cm, int stone_id, IOFormat target_format,
			  IOFormat incoming_format);
		  
/* @}*/

#ifdef	__cplusplus
}
#endif

#endif
