/* SPDX-License-Identifier: 0BSD */
#define DICE_MODULE_SLOT DICE_SLOT_FIRST
#include "dice.h"

PS_SUBSCRIBE(CHAIN_TEST, EVENT_TEST, {
    *(int *)event += 1;
    PS_PUBLISH(CHAIN_MIDDLE, type, event, md);
})

/* Publications must be attributed to LATER, even in FIRST's translation unit.
 */
PS_SUBSCRIBE_SLOT(CHAIN_MIDDLE, EVENT_TEST, DICE_SLOT_LATER, {
    *(int *)event += 1;
    PS_PUBLISH(CHAIN_OUTPUT, type, event, md);
})
