/* SPDX-License-Identifier: 0BSD */
#define DICE_MODULE_SLOT DICE_SLOT_OBSERVER
#include "dice.h"

PS_SUBSCRIBE(CAPTURE_BEFORE, EVENT_MALLOC, {
    struct malloc_event *allocation = event;
    if (allocation->size == 1234)
        puts("embedded observer: malloc(1234)");
    return PS_OK;
})
