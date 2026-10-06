/* SPDX-License-Identifier: 0BSD */
#define DICE_MODULE_SLOT DICE_SLOT_DIRECT
#include "trace.h"

PS_SUBSCRIBE(CHAIN_FLOW, EVENT_TEST, { return record(event, 'D'); })
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, EVENT_TEST, DICE_SLOT_LAST, {
    if (record(event, 'L') == PS_STOP_CHAIN)
        return PS_STOP_CHAIN;
    return PS_HANDLER_OFF;
})
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, ANY_EVENT, DICE_SLOT_LAST,
                  { return record(event, 'W'); })
