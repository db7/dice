/* SPDX-License-Identifier: 0BSD */
#define DICE_MODULE_SLOT DICE_SLOT_SOURCE
#include <assert.h>
#include <string.h>

#include "trace.h"

STATIC_ASSERT(DICE_LAST_KNOWN_SLOT == 10, "global slot boundary");

static enum ps_err
dynamic(chain_id chain, type_id type, void *event, struct metadata *md)
{
    (void)chain;
    (void)type;
    (void)md;
    return record(event, 'b');
}

static void
check(type_id type, const char *expected, char stop)
{
    struct trace trace = {.stop = stop};
    enum ps_err result = ps_publish(CHAIN_FLOW, type, &trace, NULL);
    if (strcmp(trace.text, expected))
        fprintf(stderr, "expected %s, got %s\n", expected, trace.text);
    assert(!strcmp(trace.text, expected));
    assert(result == (stop ? PS_STOP_CHAIN : PS_OK));
}

int
main(int argc, char **argv)
{
    if (argc > 1) {
        int slot   = !strcmp(argv[1], "zero") ? 0 :
                     !strcmp(argv[1], "gap")  ? 9 :
                                                DICE_LAST_KNOWN_SLOT;
        int result = ps_subscribe(CHAIN_FLOW, EVENT_TEST, dynamic, slot);
        assert(result == PS_INVALID);
        return 0;
    }
    bool plugin      = getenv("WITH_PLUGIN") != NULL;
    bool strong      = getenv("WITH_STRONG") != NULL;
    const char *tail = plugin && DICE_PLUGINS ? "TUV" : "";
    char expected[64];
    snprintf(expected, sizeof(expected), "%s%s",
             plugin ? (strong ? "AaDBLW" : "AaxByz") : (strong ? "DLW" : ""),
             tail);
    check(EVENT_TEST, expected, 0);

    assert(ps_subscribe(CHAIN_FLOW, EVENT_TEST, dynamic, DICE_SLOT_MIDDLE) ==
           PS_OK);
    snprintf(expected, sizeof(expected), "%s%s",
             plugin ? (strong ? "AaDbBLW" : "AaxbByz") :
                      (strong ? "DbLW" : "b"),
             tail);
    check(EVENT_TEST, expected, 0);
    /* Stop at every actual handler, including both sides of the phase boundary.
     */
    for (size_t i = 0; expected[i]; i++) {
        char prefix[64];
        memcpy(prefix, expected, i + 1);
        prefix[i + 1] = 0;
        check(EVENT_TEST, prefix, expected[i]);
    }
    check(EVENT_OTHER, plugin ? (strong ? "aBW" : "aBz") : (strong ? "W" : ""),
          0);
    /* The boundary is global, including on an otherwise unsubscribed chain. */
#if !DICE_CHECK_ROUTES
    assert(ps_subscribe(CHAIN_OTHER, EVENT_OTHER, dynamic, 9) == PS_INVALID);
    assert(ps_subscribe(CHAIN_FLOW, EVENT_TEST, dynamic, -1) == PS_INVALID);
#endif
    puts("two phases passed");
}
