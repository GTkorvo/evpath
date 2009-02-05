
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

static event_item *
handle_event_clone(CManager cm, event_item *event, CManager target_cm);

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
    new_event = handle_event_clone(source_cm, event, target_cm);
    internal_path_submit(target_cm, target_stone, new_event);
    CMwake_server_thread(target_cm);
}

static event_item *
handle_event_clone(CManager cm, event_item *event, CManager target_cm)
{return NULL;}
