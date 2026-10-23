#define DICE_MODULE_SLOT SLOT_APP_FAST
#include "state.h"

PS_SUBSCRIBE(CHAIN_FLOW, ANY_EVENT, {
    static unsigned serial;
    struct state *state = event;
    state->serial       = ++serial;
    state->order        = 1;
    if (state->stop)
        return PS_STOP_CHAIN;
})

#ifndef DUPLICATE
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, ANY_EVENT, SLOT_APP_NORMAL, {
    struct state *state = event;
    state->order        = state->order * 10 + 2;
})
#endif
