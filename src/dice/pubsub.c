/*
 * Copyright (C) 2025 Huawei Technologies Co., Ltd.
 * SPDX-License-Identifier: 0BSD
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef DICE_ROLL_RUNTIME
    #define DICE_MODULE_SLOT 0
    #include "tweaks.h"
#endif
#include <dice/mempool.h>
#include <dice/module.h>
#include <dice/pubsub.h>

int ps_dispatch_max(void);
bool ps_dispatch_chain_on_(chain_id);

#if !defined(DICE_ROLL_RUNTIME) || DICE_CALLBACKS
    /* Rolled configurations may map sparse public IDs to compact array indexes.
     */
    #ifndef PS_CHAIN_COUNT
        #define PS_CHAIN_COUNT     MAX_CHAINS
        #define PS_CHAIN_INDEX(ID) (ID)
    #endif
    #ifndef PS_TYPE_COUNT
        #define PS_TYPE_COUNT     MAX_TYPES
        #define PS_TYPE_INDEX(ID) (ID)
    #endif

struct sub {
    chain_id chain;
    chain_id type;
    ps_callback_f cb;
    int slot;
    struct sub *next;
};

struct type {
    size_t count;
    struct sub *head;
};

    #if !defined(DICE_ROLL_RUNTIME) || DICE_PLUGINS
struct chain {
    struct type types[PS_TYPE_COUNT];
};

static struct chain chains_[PS_CHAIN_COUNT];

static struct type *
ps_callback_list_(chain_id chain, type_id type)
{
    size_t c = PS_CHAIN_INDEX(chain), t = PS_TYPE_INDEX(type);
    if (unlikely(c >= PS_CHAIN_COUNT || !t || t >= PS_TYPE_COUNT))
        return NULL;
    return &chains_[c].types[t];
}
    #endif
#endif

// -----------------------------------------------------------------------------
// initializer
// -----------------------------------------------------------------------------

DICE_HIDE bool
ps_initd_(void)
{
    static enum {
        NONE,
        START,
        BLOCK,
    } state_ = NONE;

#if !defined(__clang__)
    static bool ready_ = false;
#endif

    if (likely(ready_)) {
        return true;
    }

    switch (state_) {
        case NONE:
            // This must be the main thread, at latest the thread creation.
            state_ = START;
            log_debug("[%4d] INIT: %s ...", DICE_MODULE_SLOT, __FILE__);
#ifndef DICE_ROLL_RUNTIME
            assert(ps_dispatch_max() >= 0);
#endif
            PS_PUBLISH(CHAIN_CONTROL, EVENT_DICE_INIT, 0, 0);
            ready_ = true;
            PS_PUBLISH(CHAIN_CONTROL, EVENT_DICE_READY, 0, 0);
            return true;
        case START:
            state_ = BLOCK;
            return true;
        case BLOCK:
            // The publication above is still running, drop nested publications.
            return false;
        default:
            __builtin_unreachable();
            return true;
    }
}

DICE_HIDE void
ps_init_(void)
{
    (void)ps_initd_();
}

/* Advertise event type names for debugging messages */
PS_ADVERTISE_TYPE(EVENT_DICE_NOP)
PS_ADVERTISE_TYPE(EVENT_DICE_READY)
PS_ADVERTISE_TYPE(EVENT_DICE_INIT)
PS_DISPATCH_SLOT_ON(DICE_MODULE_SLOT)

// -----------------------------------------------------------------------------
// subscribe interface
// -----------------------------------------------------------------------------

#if !defined(DICE_ROLL_RUNTIME) || DICE_CALLBACKS
static int
ps_subscribe_sorted_(struct sub **cur, chain_id chain, type_id type,
                     ps_callback_f cb, int slot, bool any_type)
{
    // any_type is set if this subscription was from ANY_EVENT
    struct sub *sub;
    struct sub *next = NULL;

    if (*cur == NULL || slot < (*cur)->slot) {
        next = *cur;
        goto insert;
    } else if (slot == (*cur)->slot && !any_type) {
        next = *cur;
        goto insert;
    } else {
        return ps_subscribe_sorted_(&(*cur)->next, chain, type, cb, slot,
                                    any_type);
    }

insert:
    // create and add
    sub = (struct sub *)mempool_alloc(sizeof(struct sub));
    if (!sub)
        return PS_ERROR;
    *sub = (struct sub){.chain = chain,
                        .type  = type,
                        .cb    = cb,
                        .slot  = slot,
                        .next  = next};
    *cur = sub;
    return PS_OK;
}

static int
ps_subscribe_list_(struct type *ev, chain_id chain, type_id type,
                   ps_callback_f cb, int slot, bool any_type)
{
    if (!ev)
        return PS_INVALID;

    // register subscription
    int err = ps_subscribe_sorted_(&ev->head, chain, type, cb, slot, any_type);
    if (err == PS_OK)
        ev->count++;
    return err;
}

    #if !defined(DICE_ROLL_RUNTIME) || DICE_PLUGINS
static int
ps_subscribe_type_(chain_id chain, type_id type, ps_callback_f cb, int slot,
                   bool any_type)
{
    return ps_subscribe_list_(ps_callback_list_(chain, type), chain, type, cb,
                              slot, any_type);
}

DICE_HIDE int
ps_subscribe_(chain_id chain, type_id type, ps_callback_f cb, int slot)
{
    if (chain >= MAX_CHAINS || type >= MAX_TYPES || !cb || slot < 0)
        return PS_INVALID;

        #ifndef DICE_ROLL_RUNTIME
    if (ps_dispatch_chain_on_(chain) && slot <= ps_dispatch_max())
        return PS_OK;
        #endif

    log_debug("=== subscribe %s/%s/%d", ps_chain_str(chain), ps_type_str(type),
              slot);

    if (type != ANY_EVENT)
        return ps_subscribe_type_(chain, type, cb, slot, false);

    int err;
        /* Generation supplies the declared event universe for wildcard
         * expansion.
         */
        #ifdef DICE_ROLL_RUNTIME
    for (size_t k = 0; k < sizeof(ps_event_ids_) / sizeof(ps_event_ids_[0]);
         k++) {
        type_id i = ps_event_ids_[k];
        #else
    for (size_t i = 1; i < MAX_TYPES; i++) {
        #endif
        if ((err = ps_subscribe_type_(chain, i, cb, slot, true)) != 0)
            return err;
    }

    return PS_OK;
}

        #ifndef DICE_ROLL_RUNTIME
DICE_WEAK int
ps_subscribe(chain_id chain, type_id type, ps_callback_f cb, int slot)
{
    return ps_subscribe_(chain, type, cb, slot);
}
        #endif
    #endif /* legacy / trailing callbacks */

// -----------------------------------------------------------------------------
// publish interface
// -----------------------------------------------------------------------------

static enum ps_err
ps_callback_invoke_(struct sub *cur, const chain_id chain, const type_id type,
                    void *event, struct metadata *md)
{
    while (cur) {
        // now we call the callback and abort the chain if the subscriber
        // "censors" the type by returning PS_STOP_CHAIN.
        enum ps_err err = cur->cb(chain, type, event, md);
        if (err == PS_STOP_CHAIN)
            return err;
        cur = cur->next;
    }
    return PS_OK;
}

    #if !defined(DICE_ROLL_RUNTIME) || DICE_PLUGINS
static enum ps_err
ps_callback_(const chain_id chain, const type_id type, void *event,
             struct metadata *md)
{
    struct type *ev = ps_callback_list_(chain, type);
    if (unlikely(!ev))
        return PS_INVALID;
    return ps_callback_invoke_(ev->head, chain, type, event, md);
}
    #endif
#endif /* callbacks */

#ifndef DICE_ROLL_RUNTIME
DICE_WEAK DICE_HIDE enum ps_err
ps_dispatch_(const chain_id chain, const type_id type, void *event,
             struct metadata *md)
{
    (void)chain;
    (void)type;
    (void)event;
    (void)md;
    return PS_HANDLER_OFF;
}
#endif

#if !defined(DICE_ROLL_RUNTIME) || DICE_PLUGINS
    #ifdef DICE_ROLL_RUNTIME
DICE_HIDE enum ps_err
    #else
DICE_WEAK enum ps_err
    #endif
ps_publish(const chain_id chain, const type_id type, void *event,
           struct metadata *md)
{
    log_debug("=== ps_publish %s/%s", ps_chain_str(chain), ps_type_str(type));
    if (PS_NOT_INITD_())
        return PS_STOP_CHAIN;

    log_debug("=> publishing %s/%s...", ps_chain_str(chain), ps_type_str(type));
    enum ps_err err = ps_dispatch_(chain, type, event, md);

    if (likely(err == PS_STOP_CHAIN))
        goto end;

    err = ps_callback_(chain, type, event, md);
end:
    log_debug("<= published %s/%s", ps_chain_str(chain), ps_type_str(type));
    return err;
}
#endif
