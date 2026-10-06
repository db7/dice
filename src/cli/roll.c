/* Copyright (C) 2026 Huawei Technologies Co., Ltd.
 * SPDX-License-Identifier: 0BSD */
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sxp.h>
#include <tmplr.h>
#include <unistd.h>

#include "assets.h"
#include "slots.h"
#include <sys/stat.h>

#define LIMIT    4096
#define COUNT(A) (sizeof(A) / sizeof((A)[0]))

struct identifier {
    char name[NAME_SIZE];
    int id;
    bool chain;
    bool standard;
};

struct subscription {
    char chain_name[NAME_SIZE], event_name[NAME_SIZE], handler[NAME_SIZE];
    char slot_name[NAME_SIZE];
    int chain, event, slot;
    bool builtin;
    bool plugin;
};

/* Each selected module owns a named slot in the resolved configuration. */
static const char *modules[] = {
    "self",
    "pthread_create",
    "malloc",
    "pthread_mutex",
    "pthread_cond",
    "pthread_rwlock",
    "pthread_spinlock",
    "sem",
    "mman",
    "memcpy",
    "cxa",
    "annotate_rwlock",
    "stacktrace",
    "tsan",
};
static bool embedded[COUNT(modules)];
static struct identifier ids[LIMIT];
static struct subscription subs[LIMIT];
static struct subscription prods[LIMIT];
static struct slot slots[SLOT_LIMIT];
static struct dependency {
    char before[NAME_SIZE], after[NAME_SIZE];
} deps[LIMIT];
static bool edges[SLOT_LIMIT][SLOT_LIMIT];
static size_t nids, nsubs, nprods, nslots, ndeps;
static int last_known_slot;
static bool plugins          = true, runtime_seen;
static uint64_t mempool_size = 200 * 1024 * 1024;
static char config_id[32];
static const char *input_name;

static void __attribute__((format(printf, 1, 2), noreturn))
fail(const char *fmt, ...)
{
    fprintf(stderr, "dice: %s: ", input_name ? input_name : "roll");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static const char *
asset(const char *name)
{
    for (size_t i = 0; i < COUNT(assets); i++)
        if (!strcmp(assets[i].name, name))
            return assets[i].data;
    fail("missing embedded source: %s", name);
}

static void
name_copy(char *dst, const char *src)
{
    if (!src || !*src || strlen(src) >= NAME_SIZE ||
        !(isalpha((unsigned char)*src) || *src == '_'))
        fail("invalid C identifier: %s", src ? src : "(missing)");
    for (const char *s = src; *s; s++)
        if (!(isalnum((unsigned char)*s) || *s == '_'))
            fail("invalid C identifier: %s", src);
    strcpy(dst, src);
}

static struct identifier *
find_id(const char *name, bool chain)
{
    for (size_t i = 0; i < nids; i++)
        if (ids[i].chain == chain && !strcmp(ids[i].name, name))
            return &ids[i];
    return NULL;
}

static void
add_id(const char *name, int id, bool chain, bool standard)
{
    if (nids == LIMIT)
        fail("too many identifiers (limit %d)", LIMIT);
    if (find_id(name, chain))
        fail("duplicate or reserved identifier: %s", name);
    if (!standard && strncmp(name, chain ? "CHAIN_" : "EVENT_", 6))
        fail("custom identifiers must start with %s",
             chain ? "CHAIN_" : "EVENT_");
    struct identifier *entry = &ids[nids++];
    name_copy(entry->name, name);
    entry->id       = id;
    entry->chain    = chain;
    entry->standard = standard;
}

static bool
symbol(const struct sxp *n, const char *name)
{
    return n && n->kind == SXP_SYMBOL && !strcmp(n->text, name);
}

static const char *
atom(const struct sxp *n)
{
    if (!n || n->kind != SXP_SYMBOL)
        fail("line %u: expected a symbol", n ? n->line : 0);
    return n->text;
}

static const struct sxp *
form(const struct sxp *n)
{
    if (!n || n->kind != SXP_LIST || !n->child || n->child->kind != SXP_SYMBOL)
        fail("line %u: expected a named list", n ? n->line : 0);
    return n->child;
}

static const struct sxp *
single(const struct sxp *head)
{
    if (!head->next || head->next->next)
        fail("line %u: %s expects one argument", head->line, head->text);
    return head->next;
}

static int64_t
number(const struct sxp *n, int64_t min, int64_t max)
{
    const char *s = atom(n);
    char *end;
    errno           = 0;
    long long value = strtoll(s, &end, 10);
    if (errno || end == s || *end || value < min || value > max)
        fail("line %u: expected integer in %" PRId64 "..%" PRId64, n->line, min,
             max);
    return value;
}

static void
read_ids(const struct sxp *args, bool chain)
{
    for (const struct sxp *n = args; n; n = n->next) {
        const char *name;
        int id = -1;
        if (n->kind == SXP_LIST) {
            const struct sxp *head = form(n);
            name                   = atom(head);
            id                     = (int)number(single(head), 1, 65534);
        } else {
            name = atom(n);
        }
        add_id(name, id, chain, false);
    }
}

static int
find_slot(const char *name)
{
    for (size_t i = 0; i < nslots; i++)
        if (!strcmp(slots[i].name, name))
            return (int)i;
    return -1;
}

static int
add_slot(const char *name)
{
    if (find_slot(name) >= 0)
        fail("duplicate slot: %s", name);
    if (nslots == SLOT_LIMIT)
        fail("too many slots (limit %d)", SLOT_LIMIT);
    struct slot *s = &slots[nslots];
    name_copy(s->name, name);
    for (size_t i = 0; i < nslots; i++) {
        size_t j = 0;
        while (s->name[j] && slots[i].name[j] &&
               toupper((unsigned char)s->name[j]) ==
                   toupper((unsigned char)slots[i].name[j]))
            j++;
        if (!s->name[j] && !slots[i].name[j])
            fail("slot names collide in generated macros: %s and %s", name,
                 slots[i].name);
    }
    s->number = s->forced = s->module = -1;
    return (int)nslots++;
}

static void
read_routes(const struct sxp *args, const char *slot, bool produces)
{
    for (const struct sxp *n = args; n; n = n->next) {
        const struct sxp *head = form(n);
        if (!head->next)
            fail("line %u: chain %s requires at least one event", n->line,
                 head->text);
        for (const struct sxp *event = head->next; event; event = event->next) {
            size_t *count = produces ? &nprods : &nsubs;
            if (*count == LIMIT)
                fail("too many routes");
            struct subscription *r = &(produces ? prods : subs)[(*count)++];
            name_copy(r->slot_name, slot);
            name_copy(r->chain_name, atom(head));
            name_copy(r->event_name, atom(event));
        }
    }
}

static void
read_slot(const struct sxp *head)
{
    const char *name = atom(head->next);
    int index        = add_slot(name);
    unsigned seen    = 0;
    for (const struct sxp *n = head->next->next; n; n = n->next) {
        const struct sxp *key = form(n);
        unsigned bit;
        if (symbol(key, "number"))
            bit = 1;
        else if (symbol(key, "before"))
            bit = 2;
        else if (symbol(key, "after"))
            bit = 4;
        else if (symbol(key, "consumes"))
            bit = 8;
        else if (symbol(key, "produces"))
            bit = 16;
        else if (symbol(key, "plugin") || symbol(key, "callback"))
            bit = 32;
        else
            fail("line %u: unknown slot field %s", key->line, key->text);
        if (seen & bit)
            fail("line %u: duplicate slot field %s", key->line, key->text);
        seen |= bit;
        if (bit == 1)
            slots[index].forced = (int)number(single(key), 1, INT_MAX);
        else if (bit == 32) {
            const struct sxp *value = single(key);
            if (!symbol(value, "true") && !symbol(value, "false"))
                fail("line %u: %s expects true or false", value->line,
                     key->text);
            slots[index].plugin = symbol(value, "true");
        } else if (bit == 8 || bit == 16)
            read_routes(key->next, name, bit == 16);
        else {
            if (!key->next)
                fail("line %u: empty ordering constraint", key->line);
            for (const struct sxp *ref = key->next; ref; ref = ref->next) {
                if (ndeps == LIMIT)
                    fail("too many ordering constraints");
                name_copy(deps[ndeps].before, bit == 2 ? name : atom(ref));
                name_copy(deps[ndeps++].after, bit == 2 ? atom(ref) : name);
            }
        }
    }
}

static void
read_runtime(const struct sxp *args)
{
    if (runtime_seen)
        fail("runtime may be specified in only one input file");
    runtime_seen  = true;
    unsigned seen = 0;
    for (const struct sxp *n = args; n; n = n->next) {
        const struct sxp *key = form(n);
        unsigned bit;
        if (symbol(key, "embed"))
            bit = 1;
        else if (symbol(key, "plugins"))
            bit = 2;
        else if (symbol(key, "mempool_size"))
            bit = 4;
        else
            fail("line %u: unknown runtime field %s", key->line, key->text);
        if (seen & bit)
            fail("line %u: duplicate runtime field %s", key->line, key->text);
        seen |= bit;
        if (bit == 2) {
            const struct sxp *value = single(key);
            if (!symbol(value, "true") && !symbol(value, "false"))
                fail("line %u: plugins expects true or false", value->line);
            plugins = symbol(value, "true");
        } else if (bit == 4) {
            mempool_size =
                (uint64_t)number(single(key), 4096, INT64_C(1099511627776));
        } else {
            for (const struct sxp *m = key->next; m; m = m->next) {
                size_t i;
                for (i = 0; i < COUNT(modules); i++)
                    if (!strcmp(atom(m), modules[i]))
                        break;
                if (i == COUNT(modules))
                    fail("line %u: unknown embedded module %s", m->line,
                         atom(m));
                if (embedded[i])
                    fail("duplicate embedded module: %s", modules[i]);
                embedded[i] = true;
            }
        }
    }
}

static void
read_config(const char *path, const char *ancestors[], int depth)
{
    if (depth == 32)
        fail("configuration includes exceed 32 levels");
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        fail("%s: %s", path, strerror(errno));
    for (int i = 0; i < depth; i++)
        if (!strcmp(ancestors[i], resolved))
            fail("include cycle involving %s", resolved);
    ancestors[depth] = resolved;
    FILE *file       = fopen(resolved, "r");
    if (!file)
        fail("%s: %s", resolved, strerror(errno));
    struct sxp_error error;
    struct sxp *root = sxp_read(file, &error);
    fclose(file);
    if (!root)
        fail("%s:%u: %s", resolved, error.line, error.message);
    const struct sxp *head = form(root);
    if (!symbol(head, "dice"))
        fail("%s: expected (dice ...) root", resolved);
    bool version = false;
    for (const struct sxp *n = head->next; n; n = n->next) {
        const struct sxp *key = form(n);
        if (symbol(key, "version")) {
            if (version)
                fail("duplicate version");
            (void)number(single(key), 1, 1);
            version = true;
        } else if (symbol(key, "events") || symbol(key, "chains")) {
            read_ids(key->next, symbol(key, "chains"));
        } else if (symbol(key, "runtime")) {
            read_runtime(key->next);
        } else if (symbol(key, "slot")) {
            read_slot(key);
        } else if (symbol(key, "include")) {
            if (!key->next)
                fail("include requires at least one path");
            for (const struct sxp *p = key->next; p; p = p->next) {
                if (p->kind != SXP_STRING || !*p->text)
                    fail("line %u: include expects quoted paths", p->line);
                char next[PATH_MAX];
                int len;
                if (*p->text == '/')
                    len = snprintf(next, sizeof(next), "%s", p->text);
                else
                    len = snprintf(next, sizeof(next), "%.*s/%s",
                                   (int)(strrchr(resolved, '/') - resolved),
                                   resolved, p->text);
                if (len < 0 || (size_t)len >= sizeof(next))
                    fail("include path too long");
                read_config(next, ancestors, depth + 1);
            }
        } else {
            fail("%s:%u: unknown form %s", resolved, key->line, key->text);
        }
    }
    if (!depth && !version)
        fail("expected (version 1)");
    sxp_free(root);
}

static int
id_compare(const void *a, const void *b)
{
    const struct identifier *x = a, *y = b;
    return x->chain != y->chain ? (int)x->chain - (int)y->chain :
                                  strcmp(x->name, y->name);
}

static int
sub_compare(const void *a, const void *b)
{
    const struct subscription *x = a, *y = b;
    if (x->chain != y->chain)
        return x->chain < y->chain ? -1 : 1;
    if (x->slot != y->slot)
        return x->slot < y->slot ? -1 : 1;
    return x->event - y->event;
}

static void
builtin_sub(int chain, int event, int slot)
{
    if (nsubs == LIMIT)
        fail("too many subscriptions");
    struct subscription *s = &subs[nsubs++];
    s->chain               = chain;
    s->event               = event;
    s->slot                = slot;
    s->builtin             = true;
    for (size_t i = 0; i < nslots; i++)
        if (slots[i].number == slot)
            strcpy(s->slot_name, slots[i].name);
    snprintf(s->handler, sizeof(s->handler), "ps_handler_%d_%d_%d", chain,
             event, slot);
}

static int
slot_compare(const void *a, const void *b)
{
    return strcmp(((const struct slot *)a)->name,
                  ((const struct slot *)b)->name);
}

static void
prepare_slots(void)
{
    for (size_t i = 0; i < COUNT(modules); i++) {
        if (!embedded[i])
            continue;
        char name[NAME_SIZE];
        snprintf(name, sizeof(name), "dice_%s", modules[i]);
        int index = find_slot(name);
        if (index < 0)
            index = add_slot(name);
        slots[index].module = (int)i;
        for (size_t j = 0; j < nsubs + nprods; j++) {
            struct subscription *r = j < nsubs ? &subs[j] : &prods[j - nsubs];
            if (!strcmp(r->slot_name, name))
                fail("%s consumes/produces declarations are supplied by Dice",
                     name);
        }
    }
    qsort(slots, nslots, sizeof(*slots), slot_compare);
    for (size_t i = 0; i < nslots; i++)
        if (!strncmp(slots[i].name, "dice_", 5) && slots[i].module < 0)
            fail("reserved slot %s requires its embedded module",
                 slots[i].name);
    for (size_t i = 0; i < ndeps; i++) {
        int before = find_slot(deps[i].before),
            after  = find_slot(deps[i].after);
        if (before < 0 || after < 0)
            fail("unknown slot in constraint %s -> %s", deps[i].before,
                 deps[i].after);
        edges[before][after] = true;
    }
    char error[2048];
    if (slots_resolve(slots, nslots, edges, error, sizeof(error)))
        fail("%s", error);
    for (size_t i = 0; i < nslots; i++)
        if (slots[i].number > last_known_slot)
            last_known_slot = slots[i].number;
}

static void
builtin_produces(int owner, const char *chain, const char *events)
{
    char *copy = strdup(events), *state;
    if (!copy)
        fail("out of memory");
    for (char *event = strtok_r(copy, " ", &state); event;
         event       = strtok_r(NULL, " ", &state)) {
        if (nprods == LIMIT)
            fail("too many productions");
        struct subscription *r = &prods[nprods++];
        name_copy(r->slot_name, slots[owner].name);
        name_copy(r->chain_name, chain);
        name_copy(r->event_name, event);
        r->builtin = true;
    }
    free(copy);
}

static void
builtin_routes(void)
{
    /* Possible publications, including platform-conditional interceptor APIs.
     * Keep these declarations with the interceptor catalog as it evolves. */
    const char *paired[] = {
        NULL,
        NULL,
        "EVENT_MALLOC EVENT_CALLOC EVENT_REALLOC EVENT_FREE "
        "EVENT_POSIX_MEMALIGN EVENT_ALIGNED_ALLOC",
        "EVENT_MUTEX_LOCK EVENT_MUTEX_TIMEDLOCK EVENT_MUTEX_TRYLOCK "
        "EVENT_MUTEX_UNLOCK EVENT_MUTEX_CLOCKLOCK",
        "EVENT_COND_WAIT EVENT_COND_TIMEDWAIT EVENT_COND_SIGNAL "
        "EVENT_COND_BROADCAST EVENT_COND_CLOCKWAIT",
        "EVENT_RWLOCK_RDLOCK EVENT_RWLOCK_TIMEDRDLOCK EVENT_RWLOCK_TRYRDLOCK "
        "EVENT_RWLOCK_WRLOCK EVENT_RWLOCK_TIMEDWRLOCK EVENT_RWLOCK_TRYWRLOCK "
        "EVENT_RWLOCK_UNLOCK",
        "EVENT_SPIN_LOCK EVENT_SPIN_TRYLOCK EVENT_SPIN_UNLOCK",
        "EVENT_SEM_POST EVENT_SEM_WAIT EVENT_SEM_TRYWAIT EVENT_SEM_TIMEDWAIT",
        "EVENT_MMAP EVENT_MUNMAP",
        "EVENT_MEMCPY EVENT_MEMMOVE EVENT_MEMSET",
        "EVENT_CXA_GUARD_ACQUIRE EVENT_CXA_GUARD_RELEASE EVENT_CXA_GUARD_ABORT",
        NULL,
        NULL,
        NULL,
    };
    for (size_t i = 0; i < nslots; i++) {
        int module = slots[i].module, slot = slots[i].number;
        if (module < 0)
            continue;
        builtin_sub(0, 99, slot);
        if (paired[module]) {
            builtin_produces((int)i, "INTERCEPT_BEFORE", paired[module]);
            builtin_produces((int)i, "INTERCEPT_AFTER", paired[module]);
        }
        if (module == 0) {
            builtin_sub(1, 0, slot);
            builtin_sub(2, 0, slot);
            builtin_sub(3, 0, slot);
            builtin_sub(1, 2, slot);
            builtin_produces((int)i, "CAPTURE_EVENT", "ANY_EVENT");
            builtin_produces((int)i, "CAPTURE_BEFORE", "ANY_EVENT");
            builtin_produces((int)i, "CAPTURE_AFTER", "ANY_EVENT");
        } else if (module == 1) {
            builtin_produces((int)i, "INTERCEPT_EVENT",
                             "EVENT_THREAD_START EVENT_THREAD_EXIT");
            builtin_produces((int)i, "INTERCEPT_BEFORE",
                             "EVENT_THREAD_CREATE EVENT_THREAD_JOIN");
            builtin_produces((int)i, "INTERCEPT_AFTER",
                             "EVENT_THREAD_CREATE EVENT_THREAD_JOIN");
        } else if (module == 11) {
            builtin_produces(
                (int)i, "INTERCEPT_EVENT",
                "EVENT_ANNOTATE_RWLOCK_CREATE EVENT_ANNOTATE_RWLOCK_DESTROY "
                "EVENT_ANNOTATE_RWLOCK_ACQ EVENT_ANNOTATE_RWLOCK_REL");
        } else if (module == 12) {
            builtin_produces((int)i, "INTERCEPT_EVENT",
                             "EVENT_STACKTRACE_ENTER EVENT_STACKTRACE_EXIT "
                             "EVENT_THREAD_START EVENT_THREAD_EXIT");
        } else if (module == 13) {
            builtin_produces((int)i, "INTERCEPT_EVENT",
                             "EVENT_MA_READ EVENT_MA_WRITE EVENT_MA_READ_RANGE "
                             "EVENT_MA_WRITE_RANGE");
            const char *atomic =
                "EVENT_MA_AREAD EVENT_MA_AWRITE EVENT_MA_XCHG EVENT_MA_RMW "
                "EVENT_MA_CMPXCHG EVENT_MA_FENCE";
            builtin_produces((int)i, "INTERCEPT_BEFORE", atomic);
            builtin_produces((int)i, "INTERCEPT_AFTER", atomic);
        }
    }
}

static void
resolve_routes(struct subscription *routes, size_t count, bool produces)
{
    for (size_t i = 0; i < count; i++) {
        struct subscription *r = &routes[i];
        if (!produces && r->builtin) {
            for (size_t j = 0; j < nslots; j++)
                if (slots[j].number == r->slot)
                    r->plugin = slots[j].plugin;
            if (r->plugin)
                snprintf(r->handler, sizeof(r->handler), "dice_slot_%d_%d_%d",
                         r->chain, r->event, r->slot);
            continue;
        }
        struct identifier *chain = find_id(r->chain_name, true);
        struct identifier *event = find_id(r->event_name, false);
        int owner                = find_slot(r->slot_name);
        if (!chain || !event || owner < 0)
            fail("unknown chain/event in %s: %s/%s", r->slot_name,
                 r->chain_name, r->event_name);
        r->chain  = chain->id;
        r->event  = event->id;
        r->slot   = slots[owner].number;
        r->plugin = slots[owner].plugin;
        if (!produces)
            snprintf(r->handler, sizeof(r->handler), "dice_slot_%d_%d_%d",
                     r->chain, r->event, r->slot);
    }
    qsort(routes, count, sizeof(*routes), sub_compare);
    for (size_t i = 1; i < count; i++)
        if (!sub_compare(&routes[i - 1], &routes[i]))
            fail("duplicate %s for slot %d, chain %d, event %d",
                 produces ? "production" : "consumption", routes[i].slot,
                 routes[i].chain, routes[i].event);
}

static uint64_t
hash_string(uint64_t hash, const char *value)
{
    do {
        hash ^= (unsigned char)*value;
        hash *= UINT64_C(1099511628211);
    } while (*value++);
    return hash;
}

static void
resolve(void)
{
    /* Explicit declarations only pin IDs or introduce otherwise unused names.
     * Slot endpoints supply the rest of the event and chain universe. */
    for (size_t i = 0; i < nsubs + nprods; i++) {
        struct subscription *r = i < nsubs ? &subs[i] : &prods[i - nsubs];
        if (!find_id(r->chain_name, true))
            add_id(r->chain_name, -1, true, false);
        if (!find_id(r->event_name, false))
            add_id(r->event_name, -1, false, false);
    }
    qsort(ids, nids, sizeof(*ids), id_compare);
    for (size_t i = 0; i < nids; i++) {
        if (ids[i].id < 0) {
            int candidate = ids[i].chain ? 7 : 128;
            bool used;
            do {
                used = false;
                for (size_t j = 0; j < nids; j++)
                    if (ids[j].chain == ids[i].chain && ids[j].id == candidate)
                        used = true;
                if (used)
                    candidate++;
            } while (used);
            if (candidate > 65534)
                fail("ID space exhausted");
            ids[i].id = candidate;
        }
        if (!ids[i].standard)
            for (size_t j = 0; j < nids; j++)
                if (i != j && ids[j].chain == ids[i].chain &&
                    ids[j].id == ids[i].id)
                    fail("ID collision: %s and %s", ids[i].name, ids[j].name);
    }
    prepare_slots();
    builtin_routes();
    resolve_routes(subs, nsubs, false);
    resolve_routes(prods, nprods, true);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < COUNT(assets); i++)
        hash = hash_string(hash, assets[i].data);
    char entry[256];
    for (size_t i = 0; i < nids; i++) {
        snprintf(entry, sizeof(entry), "%d:%s:%d", ids[i].chain, ids[i].name,
                 ids[i].id);
        hash = hash_string(hash, entry);
    }
    for (size_t i = 0; i < nsubs; i++) {
        snprintf(entry, sizeof(entry), "%d:%d:%d:%s", subs[i].chain,
                 subs[i].event, subs[i].slot, subs[i].handler);
        hash = hash_string(hash, entry);
    }
    for (size_t i = 0; i < nslots; i++) {
        snprintf(entry, sizeof(entry), "slot:%s:%d:%d:%d", slots[i].name,
                 slots[i].number, slots[i].module, slots[i].plugin);
        hash = hash_string(hash, entry);
    }
    for (size_t i = 0; i < nprods; i++) {
        snprintf(entry, sizeof(entry), "produces:%d:%d:%d", prods[i].chain,
                 prods[i].event, prods[i].slot);
        hash = hash_string(hash, entry);
    }
    snprintf(entry, sizeof(entry), "%d:%" PRIu64, plugins, mempool_size);
    hash = hash_string(hash, entry);
    snprintf(config_id, sizeof(config_id), "roll-1-%016" PRIx64, hash);
}

static int
sink(const char *buffer, size_t length, void *file)
{
    return fwrite(buffer, 1, length, file) != length;
}

static void
render(FILE *out, const char *source, const char *prefix, const char *key,
       const char *value)
{
    tmplr_opts opts = {.max_block_lines = 4096, .prefix = prefix};
    tmplr_ctx *ctx  = tmplr_create(&opts);
    if (!ctx)
        fail("cannot allocate template context");
    tmplr_err err = TMPLR_OK;
    if (key)
        err = tmplr_set_override(ctx, key, value);
    if (err == TMPLR_OK)
        err = tmplr_process_string(ctx, source, strlen(source), sink, out);
    tmplr_destroy(ctx);
    if (err != TMPLR_OK)
        fail("template expansion failed: %s", tmplr_strerror(err));
}

static bool
first_id(size_t index)
{
    for (size_t i = 0; i < index; i++)
        if (ids[i].chain == ids[index].chain && ids[i].id == ids[index].id)
            return false;
    return true;
}

static void
emit_registry(FILE *out, bool chain)
{
    const char *kind = chain ? "chain" : "event";
    fprintf(out, "static bool dice_%s_valid_(%s_id id)\n{\n    switch (id) {\n",
            kind, chain ? "chain" : "type");
    for (size_t i = 0; i < nids; i++)
        if (ids[i].chain == chain && first_id(i) && (chain || ids[i].id))
            fprintf(out, "    case %d:\n", ids[i].id);
    fputs("        return true;\n    default: return false;\n    }\n}\n", out);
    fprintf(
        out,
        "static const char *dice_%s_name_(%s_id id)\n{\n    switch (id) {\n",
        kind, chain ? "chain" : "type");
    for (size_t i = 0; i < nids; i++)
        if (ids[i].chain == chain && first_id(i))
            fprintf(out, "    case %d: return \"%s\";\n", ids[i].id,
                    ids[i].name);
    fputs("    default: return \"<unknown>\";\n    }\n}\n", out);
}

static void
emit_sequence(FILE *out, int chain, int event)
{
    for (size_t i = 0; i < nsubs;) {
        int slot = subs[i].slot, current_chain = subs[i].chain;
        const char *specific = NULL, *wildcard = NULL;
        do {
            if (subs[i].event == 0)
                wildcard = subs[i].handler;
            else if (subs[i].event == event)
                specific = subs[i].handler;
            i++;
        } while (i < nsubs && subs[i].slot == slot &&
                 subs[i].chain == current_chain);
        if (current_chain != chain || (!specific && !wildcard))
            continue;
        render(out, asset("call"), "$", "ROLL_HANDLER",
               specific ? specific : wildcard);
        if (specific && wildcard) {
            fputs("        if (err == PS_HANDLER_OFF) {\n", out);
            render(out, asset("call"), "$", "ROLL_HANDLER", wildcard);
            fputs("        }\n", out);
        }
        fputs("        if (err == PS_STOP_CHAIN) return err;\n", out);
    }
    fputs("        return PS_OK;\n", out);
}

static void
emit_dispatch(FILE *out)
{
    fputs(
        "DICE_HIDE enum ps_err\nps_dispatch_(chain_id chain, type_id type, "
        "void *event, struct metadata *md)\n{\n"
        "    enum ps_err err = PS_OK;\n"
        "    (void)err; (void)type; (void)event; (void)md;\n"
        "    switch (chain) {\n",
        out);
    for (size_t c = 0; c < nids; c++) {
        if (!ids[c].chain || !first_id(c))
            continue;
        bool subscribed = false;
        for (size_t i = 0; i < nsubs; i++)
            if (subs[i].chain == ids[c].id)
                subscribed = true;
        if (!subscribed)
            continue;
        fprintf(out, "    case %d:\n        switch (type) {\n", ids[c].id);
        for (size_t e = 0; e < nids; e++) {
            if (ids[e].chain || !ids[e].id || !first_id(e))
                continue;
            bool exact = false;
            for (size_t i = 0; i < nsubs; i++)
                if (subs[i].chain == ids[c].id && subs[i].event == ids[e].id)
                    exact = true;
            if (!exact)
                continue;
            fprintf(out, "        case %d:\n", ids[e].id);
            emit_sequence(out, ids[c].id, ids[e].id);
        }
        fputs("        default:\n", out);
        emit_sequence(out, ids[c].id, 0);
        fputs("        }\n", out);
    }
    fputs("    default: return PS_OK;\n    }\n}\n", out);
}

static void
emit_route_expression(FILE *out, const struct subscription *routes,
                      size_t count, const char *slot, const char *chain,
                      const char *event)
{
    fputs("(0", out);
    for (size_t i = 0; i < count; i++) {
        fprintf(out, " || ((%s) == %d && (%s) == %d", slot, routes[i].slot,
                chain, routes[i].chain);
        if (routes[i].event)
            fprintf(out, " && (%s) == %d", event, routes[i].event);
        fputc(')', out);
    }
    fputc(')', out);
}

static void
emit_route_checks(FILE *out)
{
    fputs("#if DICE_CHECK_ROUTES\n", out);
    for (int produces = 0; produces < 2; produces++) {
        fprintf(out,
                "static bool dice_%s_allowed_(int slot, chain_id chain, "
                "type_id type)\n{\n"
                "    (void)slot; (void)chain; (void)type;\n    return ",
                produces ? "produces" : "consumes");
        emit_route_expression(out, produces ? prods : subs,
                              produces ? nprods : nsubs, "slot", "chain",
                              "type");
        fputs(";\n}\n", out);
    }
    fputs("#endif\n", out);
}

static void
emit_header(FILE *out)
{
    bool callbacks = plugins;
    for (size_t i = 0; i < nsubs; i++)
        callbacks |= subs[i].plugin;
    fputs(
        "/* Generated by dice roll. Rebuild runtime and plugins together. */\n"
        "#ifndef DICE_GENERATED_H\n#define DICE_GENERATED_H\n"
        "#if defined(DICE_MODULE_H) || defined(DICE_TYPES_H)\n"
        "#error Include generated dice.h before legacy Dice headers\n#endif\n"
        "#define DICE_GENERATED_IDS 1\n"
        "#ifndef _GNU_SOURCE\n#define _GNU_SOURCE\n#endif\n",
        out);
    fprintf(out,
            "#ifndef DICE_CHECK_ROUTES\n#define DICE_CHECK_ROUTES 0\n#endif\n"
            "#if DICE_CHECK_ROUTES\n#define DICE_CONFIG_ID \"%s-checked\"\n"
            "#else\n#define DICE_CONFIG_ID \"%s\"\n#endif\n"
            "#define DICE_PLUGINS %d\n",
            config_id, config_id, plugins);
    fprintf(out, "#define DICE_CONFIG_HASH 0x%sULL\n", config_id + 7);
    fprintf(out,
            "#define DICE_CALLBACKS %d\n"
            "#define DICE_LAST_KNOWN_SLOT %d\n"
            "#define DICE_DEFAULT_PLUGIN_SLOT %d\n",
            callbacks, last_known_slot,
            last_known_slot == INT_MAX ? 0 :
            last_known_slot < 10000    ? 10000 :
                                         last_known_slot + 1);
    for (size_t i = 0; i < nslots; i++) {
        char name[NAME_SIZE];
        strcpy(name, slots[i].name);
        for (char *p = name; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        fprintf(out, "#define DICE_SLOT_%s %d\n", name, slots[i].number);
        if (slots[i].module < 0 && !slots[i].plugin)
            fprintf(out, "#define DICE_STATIC_SLOT_%d 1\n", slots[i].number);
    }
    for (size_t i = 0; i < nsubs; i++)
        fprintf(out, "#define dice_consumes_%d_%d_%d 1\n", subs[i].chain,
                subs[i].event, subs[i].slot);
    fputs("#if DICE_CHECK_ROUTES\n#define DICE_ROUTE_CONSUMES(S, C, E) ", out);
    emit_route_expression(out, subs, nsubs, "S", "C", "E");
    fputs("\n#endif\n", out);
    int max_event = 127, max_chain = 6;
    for (size_t i = 0; i < nids; i++) {
        int *max = ids[i].chain ? &max_chain : &max_event;
        if (ids[i].id > *max)
            *max = ids[i].id;
        fprintf(out, "#define %s %d\n", ids[i].name, ids[i].id);
    }
    fprintf(out, "#define MAX_TYPES %d\n#define MAX_CHAINS %d\n", max_event + 1,
            max_chain + 1);
    fputs(asset("public"), out);
    fputs("#ifdef DICE_ROLL_BUILTIN\n", out);
    fputs(asset("private"), out);
    fputs("#endif\n", out);
    fputs(asset("module"), out);
    for (size_t i = 0; i < nsubs; i++)
        if (!subs[i].builtin)
            fprintf(out,
                    "DICE_HIDE enum ps_err %s(chain_id, type_id, void *, "
                    "struct metadata "
                    "*);\n",
                    subs[i].handler);
    fputs("#endif /* DICE_GENERATED_H */\n", out);
}

static void
emit_callback_index(FILE *out, bool chain)
{
    const char *kind = chain ? "CHAIN" : "TYPE";
    int maximum = 0, count = 0;
    for (size_t i = 0; i < nids; i++) {
        if (ids[i].chain != chain || !first_id(i))
            continue;
        count++;
        if (ids[i].id > maximum)
            maximum = ids[i].id;
    }
    /* Preserve direct indexing for small/dense IDs; compact large forced IDs
     * so one high event and chain cannot create a multi-gigabyte matrix. */
    if (maximum < (chain ? 64 : 256) || maximum < count * 2) {
        fprintf(out, "#define PS_%s_COUNT %d\n#define PS_%s_INDEX(ID) (ID)\n",
                kind, maximum + 1, kind);
        return;
    }
    fprintf(out, "#define PS_%s_COUNT %d\n", kind, count);
    fprintf(out,
            "static size_t ps_%s_index_(unsigned id)\n{\n"
            "    switch (id) {\n",
            kind);
    int index = chain ? 0 : 1;
    for (size_t i = 0; i < nids; i++) {
        if (ids[i].chain != chain || !first_id(i))
            continue;
        int mapped = !chain && !ids[i].id ? 0 : index++;
        fprintf(out, "    case %d: return %d;\n", ids[i].id, mapped);
    }
    fprintf(out,
            "    default: return PS_%s_COUNT;\n    }\n}\n"
            "#define PS_%s_INDEX(ID) ps_%s_index_(ID)\n",
            kind, kind, kind);
}

static void
emit_callback_call(FILE *out, int chain, int slot, const char *index)
{
    fprintf(out,
            "    err = ps_callback_invoke_(dice_callbacks_%d_%d[%s].head, "
            "chain, type, event, md);\n"
            "    if (err == PS_STOP_CHAIN) return err;\n",
            chain, slot, index);
}

static int
callback_type_index(int type)
{
    int maximum = 0, count = 0;
    for (size_t i = 0; i < nids; i++) {
        if (ids[i].chain || !first_id(i))
            continue;
        count++;
        if (ids[i].id > maximum)
            maximum = ids[i].id;
    }
    if (maximum < 256 || maximum < count * 2)
        return type;
    int index = 1;
    for (size_t i = 0; i < nids; i++) {
        if (ids[i].chain || !first_id(i))
            continue;
        int mapped = !ids[i].id ? 0 : index++;
        if (ids[i].id == type)
            return mapped;
    }
    fail("cannot index callback event %d", type);
    return 0;
}

static void
emit_known_callbacks(FILE *out)
{
    /* Weak definitions belong only in dice.c: putting the weak attribute on
     * header prototypes would also weaken the linked module definitions. */
    for (size_t i = 0; i < nsubs;) {
        size_t first = i;
        int chain = subs[i].chain, slot = subs[i].slot;
        do {
            i++;
        } while (i < nsubs && subs[i].chain == chain && subs[i].slot == slot);
        if (subs[first].builtin && !subs[first].plugin)
            continue;
        fputs("#if DICE_CALLBACKS\n", out);
        /* Embedded interceptors can publish before constructors or libc have
         * initialized. Bind their callbacks statically, without allocation. */
        if (subs[first].builtin)
            for (size_t j = first; j < i; j++)
                fprintf(out,
                        "static struct sub dice_initial_%d_%d_%d = {\n"
                        "    .chain = %d, .type = %d, .slot = %d,\n"
                        "    .cb = ps_handler_%d_%d_%d\n};\n",
                        chain, subs[j].event, slot, chain, subs[j].event, slot,
                        chain, subs[j].event, slot);
        fprintf(out,
                "static struct type dice_callbacks_%d_%d[PS_TYPE_COUNT] = {\n",
                chain, slot);
        if (subs[first].builtin) {
            for (size_t j = first; j < i; j++)
                fprintf(out,
                        "    [%d] = {.count = 1, .head = "
                        "&dice_initial_%d_%d_%d},\n",
                        callback_type_index(subs[j].event), chain,
                        subs[j].event, slot);
        } else {
            fputs("    {0}\n", out);
        }
        fputs("};\n#endif\n", out);
        for (size_t j = first; j < i; j++) {
            fprintf(out,
                    "DICE_WEAK DICE_HIDE enum ps_err %s(chain_id chain, "
                    "type_id type, void *event, struct metadata *md)\n{\n"
                    "    (void)chain; (void)type; (void)event; (void)md;\n"
                    "#if DICE_CALLBACKS\n"
                    "    size_t index = PS_TYPE_INDEX(type);\n"
                    "    if (!index || index >= PS_TYPE_COUNT) return "
                    "PS_HANDLER_OFF;\n"
                    "    enum ps_err err;\n",
                    subs[j].handler);
            if (!subs[j].event) {
                /* Explicit cases already invoked their exact handler. The
                 * wildcard fallback also serves dynamically registered exact
                 * events that have no explicit case in this slot. */
                fputs("    if (1", out);
                for (size_t k = first; k < i; k++)
                    if (subs[k].event)
                        fprintf(out, " && type != %d", subs[k].event);
                fputs(") {\n", out);
                emit_callback_call(out, chain, slot, "index");
                fputs("    }\n", out);
                emit_callback_call(out, chain, slot, "0");
            } else {
                emit_callback_call(out, chain, slot, "index");
            }
            fputs("#endif\n    return PS_HANDLER_OFF;\n}\n", out);
        }
    }
    fputs(
        "static int dice_subscribe_known_(chain_id chain, type_id type, "
        "ps_callback_f callback, int slot)\n{\n"
        "    (void)chain; (void)type; (void)callback; (void)slot;\n"
        "#if DICE_CALLBACKS\n",
        out);
    for (size_t i = 0; i < nsubs;) {
        size_t first = i;
        int chain = subs[i].chain, slot = subs[i].slot;
        bool wildcard = false;
        do {
            wildcard |= !subs[i].event;
            i++;
        } while (i < nsubs && subs[i].chain == chain && subs[i].slot == slot);
        if (subs[first].builtin && !subs[first].plugin)
            continue;
        fprintf(out, "    if (slot == %d && chain == %d) {\n", slot, chain);
        if (!wildcard) {
            fputs("        if (!(0", out);
            for (size_t j = first; j < i; j++)
                fprintf(out, " || type == %d", subs[j].event);
            fputs(")) return PS_INVALID;\n", out);
        }
        fprintf(out, "        struct type *heads = dice_callbacks_%d_%d;\n",
                chain, slot);
        fputs(
            "        size_t index = PS_TYPE_INDEX(type);\n"
            "        if (index >= PS_TYPE_COUNT) return PS_INVALID;\n"
            "        return ps_subscribe_list_(&heads[index], chain, type, "
            "callback, slot, type == ANY_EVENT);\n    }\n",
            out);
    }
    fputs("#endif\n    return PS_INVALID;\n}\n", out);
}

static void
emit_pubsub(FILE *out)
{
    fputs(
        "\n#define DICE_ROLL_RUNTIME\n"
        "#undef DICE_MODULE_SLOT\n#define DICE_MODULE_SLOT 0\n"
        "#undef ps_subscribe\n#undef ps_publish\n"
        "DICE_HIDE enum ps_err ps_dispatch_(chain_id, type_id, void *, "
        "struct metadata *);\n"
        "DICE_HIDE enum ps_err dice_publish_internal_(chain_id, type_id, "
        "void *, struct metadata *);\n"
        "#define ps_publish dice_publish_internal_\n",
        out);
    bool callbacks = plugins;
    for (size_t i = 0; i < nsubs; i++)
        callbacks |= subs[i].plugin;
    if (callbacks) {
        if (plugins)
            emit_callback_index(out, true);
        emit_callback_index(out, false);
        if (plugins) {
            fputs("static const type_id ps_event_ids_[] = {\n", out);
            for (size_t i = 0; i < nids; i++)
                if (!ids[i].chain && ids[i].id && first_id(i))
                    fprintf(out, "    %d,\n", ids[i].id);
            fputs("};\n", out);
        }
    }
    fputs(asset("pubsub-tweaks"), out);
    fputs(asset("pubsub"), out);
    emit_known_callbacks(out);
    if (!plugins)
        fputs(asset("pubsub-box"), out);
    fputs("\n#undef ps_publish\n", out);
}

static void
emit_source(FILE *out)
{
    fputs(
        "/* Generated by dice roll. */\n#define DICE_ROLL_BUILTIN\n"
        "#include \"dice.h\"\n#include <string.h>\n"
        "/* Embedded release modules use the core's internal fast path.\n"
        " * Checked modules retain the header's per-slot route validation. */\n"
        "#if !DICE_CHECK_ROUTES\n"
        "DICE_HIDE enum ps_err dice_publish_internal_(chain_id, type_id, "
        "void *, struct metadata *);\n"
        "#define ps_publish dice_publish_internal_\n#endif\n",
        out);
    fprintf(out,
            "#if !defined(__APPLE__) && !defined(__NetBSD__)\n"
            "#define DICE_MEMPOOL_USE_MMAP\n#endif\n"
            "#define MEMPOOL_SIZE UINT64_C(%" PRIu64 ")\n",
            mempool_size);
    fprintf(out,
            "_Static_assert(DICE_CONFIG_HASH == 0x%sULL, "
            "\"dice.c and dice.h are from different configurations\");\n",
            config_id + 7);
    fputs(asset("mempool"), out);
    for (size_t i = 0; i < COUNT(modules); i++) {
        if (!embedded[i])
            continue;
        int number = -1;
        for (size_t j = 0; j < nslots; j++)
            if (slots[j].module == (int)i)
                number = slots[j].number;
        fprintf(out, "\n#undef DICE_MODULE_SLOT\n#define DICE_MODULE_SLOT %d\n",
                number);
        fprintf(out,
                "#if DICE_CHECK_ROUTES\n#undef DICE_ROUTE_SLOT\n"
                "#define DICE_ROUTE_SLOT dice_route_slot_%d\n"
                "enum { DICE_ROUTE_SLOT = %d };\n#endif\n",
                number, number);
        if (!strcmp(modules[i], "pthread_spinlock"))
            fputs(
                "#if defined(__APPLE__)\n#error pthread_spinlock is not "
                "available on macOS\n#endif\n",
                out);
        if (!strcmp(modules[i], "tsan"))
            render(out, asset(modules[i]), "_tmpl", NULL, NULL);
        else
            fputs(asset(modules[i]), out);
    }
    emit_route_checks(out);
    emit_registry(out, false);
    emit_registry(out, true);
    emit_pubsub(out);
    fputs(asset("runtime"), out);
    emit_dispatch(out);
}

static void
generate(const char *directory)
{
    if (mkdir(directory, 0755) && errno != EEXIST)
        fail("%s: %s", directory, strerror(errno));
    char tmp_c[PATH_MAX], tmp_h[PATH_MAX], dest_c[PATH_MAX], dest_h[PATH_MAX];
    if (snprintf(dest_c, sizeof(dest_c), "%s/dice.c", directory) >=
            (int)sizeof(dest_c) ||
        snprintf(dest_h, sizeof(dest_h), "%s/dice.h", directory) >=
            (int)sizeof(dest_h) ||
        snprintf(tmp_c, sizeof(tmp_c), "%s/.dice.c.XXXXXX", directory) >=
            (int)sizeof(tmp_c) ||
        snprintf(tmp_h, sizeof(tmp_h), "%s/.dice.h.XXXXXX", directory) >=
            (int)sizeof(tmp_h))
        fail("output path is too long");
    int fd_c = mkstemp(tmp_c), fd_h = mkstemp(tmp_h);
    if (fd_c < 0 || fd_h < 0)
        fail("cannot create output: %s", strerror(errno));
    FILE *c = fdopen(fd_c, "w"), *h = fdopen(fd_h, "w");
    if (!c || !h)
        fail("cannot open output streams: %s", strerror(errno));
    emit_header(h);
    emit_source(c);
    bool error = ferror(c) || ferror(h);
    if (fclose(c))
        error = true;
    if (fclose(h))
        error = true;
    if (error || chmod(tmp_c, 0644) || chmod(tmp_h, 0644)) {
        unlink(tmp_c);
        unlink(tmp_h);
        fail("cannot write output: %s", strerror(errno));
    }
    if (rename(tmp_h, dest_h) || rename(tmp_c, dest_c))
        fail("cannot replace output: %s", strerror(errno));
}

static void
usage(FILE *out)
{
    fputs(
        "Usage: dice roll CONFIG.dice --output DIRECTORY\n"
        "       dice check CONFIG.dice\n"
        "       dice query CONFIG.dice\n"
        "       dice graph CONFIG.dice\n"
        "       dice modules\n",
        out);
}

int
main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h"))) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("dice roll 1 (configuration schema 1)");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--licenses")) {
        puts(asset("licenses"));
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "modules")) {
        for (size_t i = 0; i < COUNT(modules); i++)
            puts(modules[i]);
        return 0;
    }
    if (argc < 3) {
        usage(stderr);
        return 1;
    }
    bool roll  = !strcmp(argv[1], "roll");
    bool query = !strcmp(argv[1], "query");
    bool graph = !strcmp(argv[1], "graph");
    bool check = !strcmp(argv[1], "check");
    if ((!roll && !query && !graph && !check) ||
        (roll && (argc != 5 ||
                  (strcmp(argv[3], "--output") && strcmp(argv[3], "-o")))) ||
        (!roll && argc != 3)) {
        usage(stderr);
        return 1;
    }
    input_name = argv[2];
    for (size_t i = 0; i < COUNT(standard_ids); i++)
        add_id(standard_ids[i].name, standard_ids[i].id, standard_ids[i].chain,
               true);
    const char *ancestors[32];
    read_config(input_name, ancestors, 0);
    resolve();
    if (roll)
        generate(argv[4]);
    else if (query) {
        printf("configuration %s\nplugins %s\n", config_id,
               plugins ? "true" : "false");
        for (size_t i = 0; i < nids; i++)
            printf("%s %s %d\n", ids[i].chain ? "chain" : "event", ids[i].name,
                   ids[i].id);
        for (size_t i = 0; i < nslots; i++)
            printf("slot %s %d%s%s\n", slots[i].name, slots[i].number,
                   slots[i].forced >= 0 ? " forced" : "",
                   slots[i].plugin ? " callback" : "");
        for (size_t i = 0; i < nsubs; i++)
            printf("consumes %s %d/%d\n", subs[i].slot_name, subs[i].chain,
                   subs[i].event);
        for (size_t i = 0; i < nprods; i++)
            printf("produces %s %d/%d\n", prods[i].slot_name, prods[i].chain,
                   prods[i].event);
    } else if (graph) {
        puts("digraph dice {");
        for (size_t i = 0; i < nslots; i++) {
            printf("  \"slot:%s\" [label=\"%s (%d)\"];\n", slots[i].name,
                   slots[i].name, slots[i].number);
            for (size_t j = 0; j < nslots; j++)
                if (edges[i][j])
                    printf(
                        "  \"slot:%s\" -> \"slot:%s\" "
                        "[style=dashed,label=\"before\"];\n",
                        slots[i].name, slots[j].name);
        }
        for (size_t i = 0; i < nsubs; i++)
            printf("  \"chain:%d\" -> \"slot:%s\" [label=\"event %d\"];\n",
                   subs[i].chain, subs[i].slot_name, subs[i].event);
        for (size_t i = 0; i < nprods; i++)
            printf("  \"slot:%s\" -> \"chain:%d\" [label=\"event %d\"];\n",
                   prods[i].slot_name, prods[i].chain, prods[i].event);
        puts("}");
    }
    return 0;
}
