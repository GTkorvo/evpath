
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

/*
 *  This file is meant to capture all the points at which CM/EVPath
 *  instances in a shared address space might interact.  Generally, there is
 *  a single lock that protects the data of each instance.  The
 *  cm_interface.c file, generated from evpath.h by gen_interface.pl makes
 *  sure that all entry points appropriately acquire and release the lock for
 *  the CM on which they operate.  Any thread associated with the CM (like
 *  the communications thread) should also acquire the lock when it is
 *  operating on protected data and drop it when it is waiting on
 *  communication, or when control passes to user/handler.
 *
 *  Passing events between instances requires modifying data structures in
 *  each as queues are modified.  Some data in events may be eventually be
 *  shared or referenced in multiple CMs.  Code that modifies potentially
 *  shared data should live in this file.
 */

static event_item *
clone_event(CManager cm, event_item *event, CManager target_cm);

extern void 
thread_bridge_transfer(CManager source_cm, event_item *event, 
		       CManager target_cm, EVstone target_stone)
{
    event_item *new_event;
    if (target_cm == source_cm) {
	internal_path_submit(source_cm, target_stone, event);
	return;
    } else if (target_cm > source_cm) {
	/* source_cm should already be locked, lock the destination */
	assert(CManager_locked(source_cm));
	CManager_lock(target_cm);
    } else {
	/* 
	 * we want to lock the CM's in smallest-first order.
	 * source_cm is larger, so unlock that and then re-aquire the locks.
	 * strict ordering avoids deadlock in case they are transferring 
	 * something to us.
	 */
	CManager_unlock(source_cm);
	CManager_lock(target_cm);
	CManager_lock(source_cm);
    }
    /* Both CMs are locked now */
    new_event = clone_event(source_cm, event, target_cm);
    internal_path_submit(target_cm, target_stone, new_event);
    CManager_unlock(target_cm);
    CMwake_server_thread(target_cm);
}

static event_item *
clone_event(CManager cm, event_item *event, CManager target_cm)
{
    switch(event->contents) {
    case Event_CM_Owned:
	/* 
	 * Just return the event.  We'll ensure the CMbuffer gets
	 * assigned back to the original CM when we do the release.  (If
	 * we don't get it back, we risk a permanent transfer of buffers
	 * between CMs.  Possibly very bad if one is always the network
	 * manager and the other the consumer.)
	 */
	return event;
    case Event_Freeable:
	/* 
	 *  Ditto.  Except that it doesn't matter much who calls the 
	 *  free function.
	 */
	return event;
    case Event_App_Owned:
         /*
	  * Ugly.  This is a local EVsubmit of user-owned data.  They 
	  * expect us not to return until they can overwrite their data.
	  * Cheat and force the event to be CM-owned instead.
	  */
	ensure_ev_owned(cm, event);
	return event;
    }
}
