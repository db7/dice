/* SPDX-License-Identifier: 0BSD */
#define DICE_PLUGIN_MODULE
#include "trace.h"

PS_SUBSCRIBE_SLOT(CHAIN_FLOW, EVENT_TEST, DICE_SLOT_EARLY,
                  { return record(event, 'A'); })
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, ANY_EVENT, DICE_SLOT_EARLY,
                  { return record(event, 'a'); })
/* Register even when a strong linked definition replaces this fallback. */
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, EVENT_TEST, DICE_SLOT_DIRECT,
                  { return record(event, 'x'); })
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, ANY_EVENT, DICE_SLOT_MIDDLE,
                  { return record(event, 'B'); })
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, EVENT_TEST, DICE_SLOT_LAST,
                  { return record(event, 'y'); })
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, ANY_EVENT, DICE_SLOT_LAST,
                  { return record(event, 'z'); })

#if DICE_PLUGINS
/* Register out of order: the trailing phase must still run in slot order. */
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, EVENT_TEST, 14, { return record(event, 'U'); })
PS_SUBSCRIBE_SLOT(CHAIN_FLOW, EVENT_TEST, 11, {
    PS_PUBLISH(CHAIN_OTHER, EVENT_OTHER, event, md);
    return record(event, 'T');
})
PS_SUBSCRIBE(CHAIN_FLOW, EVENT_TEST, { return record(event, 'V'); })
#endif
