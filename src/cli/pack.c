/* SPDX-License-Identifier: 0BSD
 * Build-host source packer. The installed dice executable needs no source tree.
 */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILES 2048
static char root[PATH_MAX];
static char seen[MAX_FILES][PATH_MAX];
static size_t nseen;
static FILE *output;
static char asset_names[64][128];
static size_t nassets;
static struct definition {
    char name[128], value[128];
} defs[512];
static size_t ndefs;

static void
die(const char *message, const char *path)
{
    fprintf(stderr, "dice-pack: %s: %s\n", message, path);
    exit(EXIT_FAILURE);
}

static FILE *
open_file(const char *path, const char *mode)
{
    FILE *file = fopen(path, mode);
    if (!file)
        die("cannot open", path);
    return file;
}

static void
path_join(char *dest, const char *base, const char *name)
{
    if (snprintf(dest, PATH_MAX, "%s/%s", base, name) >= PATH_MAX)
        die("path too long", name);
}

static void
copy(FILE *dest, FILE *source)
{
    int c;
    while ((c = fgetc(source)) != EOF)
        fputc(c, dest);
    if (ferror(source))
        die("read failed", "source asset");
}

static void
copy_path(FILE *dest, const char *relative)
{
    char path[PATH_MAX];
    path_join(path, root, relative);
    FILE *file = open_file(path, "r");
    copy(dest, file);
    fclose(file);
}

static bool
include_line(const char *line, char *name, bool *quoted)
{
    char directive[32], delim;
    int offset;
    if (sscanf(line, " #%31s %c%n", directive, &delim, &offset) != 2 ||
        strcmp(directive, "include") || (delim != '<' && delim != '"'))
        return false;
    const char *end = strchr(line + offset, delim == '<' ? '>' : '"');
    if (!end || end - (line + offset) >= PATH_MAX)
        die("invalid include", line);
    size_t len = (size_t)(end - (line + offset));
    memcpy(name, line + offset, len);
    name[len] = 0;
    *quoted   = delim == '"';
    return true;
}

static void
collect_definition(const char *line)
{
    char key[128], value[128], extra;
    if (sscanf(line, " #define %127s %127s %c", key, value, &extra) != 2 ||
        (strncmp(key, "EVENT_", 6) && strncmp(key, "CAPTURE_", 8) &&
         strncmp(key, "INTERCEPT_", 10)) ||
        (!isdigit((unsigned char)value[0]) && strncmp(value, "EVENT_", 6)))
        return;
    for (size_t i = 0; i < ndefs; i++)
        if (!strcmp(defs[i].name, key))
            return;
    if (ndefs == sizeof(defs) / sizeof(*defs))
        die("too many standard identifiers", key);
    strcpy(defs[ndefs].name, key);
    strcpy(defs[ndefs++].value, value);
}

static void
expand(FILE *dest, const char *path, bool conditional, const char *stack[],
       unsigned level)
{
    if (level == 128)
        die("include nesting too deep", path);
    char resolved[PATH_MAX];
    if (!realpath(path, resolved))
        die("cannot resolve", path);
    for (size_t i = 0; i < nseen; i++)
        if (!strcmp(seen[i], resolved))
            return;
    for (unsigned i = 0; i < level; i++)
        if (!strcmp(stack[i], resolved))
            return;
    stack[level] = resolved;
    FILE *file   = open_file(resolved, "r");
    char line[8192], directive[32];
    bool guarded = false;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, " #%31s", directive) == 1) {
            guarded = !strcmp(directive, "ifndef");
            break;
        }
    }
    rewind(file);
    if (guarded && !conditional) {
        if (nseen == MAX_FILES)
            die("too many headers", path);
        strcpy(seen[nseen++], resolved);
    }
    int depth = 0;
    while (fgets(line, sizeof(line), file)) {
        collect_definition(line);
        char name[PATH_MAX], candidate[PATH_MAX], found[PATH_MAX];
        bool quoted;
        if (include_line(line, name, &quoted)) {
            found[0] = 0;
            if (quoted) {
                char parent[PATH_MAX];
                strcpy(parent, resolved);
                *strrchr(parent, '/') = 0;
                path_join(candidate, parent, name);
                if (!realpath(candidate, found))
                    found[0] = 0;
            }
            const char *bases[] = {"include", "deps/libvsync/include"};
            for (size_t i = 0; !found[0] && i < 2; i++) {
                char base[PATH_MAX];
                path_join(base, root, bases[i]);
                path_join(candidate, base, name);
                if (!realpath(candidate, found))
                    found[0] = 0;
            }
            if (found[0]) {
                expand(dest, found, conditional || depth > (int)guarded, stack,
                       level + 1);
                continue;
            }
        }
        if (sscanf(line, " #%31s", directive) == 1) {
            if (!strcmp(directive, "if") || !strcmp(directive, "ifdef") ||
                !strcmp(directive, "ifndef"))
                depth++;
            if (!strcmp(directive, "endif"))
                depth--;
        }
        fputs(line, dest);
    }
    if (ferror(file))
        die("read failed", path);
    fclose(file);
}

static void
expand_relative(FILE *dest, const char *path)
{
    char full[PATH_MAX];
    const char *stack[128];
    path_join(full, root, path);
    expand(dest, full, false, stack, 0);
}

static FILE *
begin(const char *name)
{
    if (nassets == sizeof(asset_names) / sizeof(*asset_names) ||
        strlen(name) >= 128)
        die("too many/long assets", name);
    strcpy(asset_names[nassets], name);
    FILE *file = tmpfile();
    if (!file)
        die("cannot create temporary file", name);
    return file;
}

static void
finish(FILE *file)
{
    if (fflush(file) || ferror(file))
        die("cannot write temporary asset", asset_names[nassets]);
    rewind(file);
    fprintf(output, "static const char asset_%zu[] =\n\"", nassets++);
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (c == '\n')
            fputs("\\n\"\n\"", output);
        else if (c == '"' || c == '\\')
            fprintf(output, "\\%c", c);
        else if (c < 32 || c >= 127)
            fprintf(output, "\\%03o", c);
        else
            fputc(c, output);
    }
    fputs("\";\n", output);
    fclose(file);
}

static int
compare(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static size_t
list(const char *relative, const char *suffix, char *names[])
{
    char path[PATH_MAX];
    path_join(path, root, relative);
    DIR *dir = opendir(path);
    if (!dir)
        die("cannot list", path);
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        size_t len = strlen(entry->d_name), end = strlen(suffix);
        if (len <= end || strcmp(entry->d_name + len - end, suffix))
            continue;
        if (count == MAX_FILES)
            die("too many files", relative);
        names[count] = strdup(entry->d_name);
        if (!names[count++])
            die("out of memory", relative);
    }
    closedir(dir);
    qsort(names, count, sizeof(*names), compare);
    return count;
}

static void
source(const char *name, const char *relative)
{
    FILE *asset = begin(name);
    char path[PATH_MAX];
    path_join(path, root, relative);
    FILE *file = open_file(path, "r");
    char line[8192], header[PATH_MAX];
    bool quoted;
    while (fgets(line, sizeof(line), file)) {
        if (include_line(line, header, &quoted) &&
            (!strncmp(header, "dice/", 5) || !strncmp(header, "vsync/", 6)))
            continue;
        fputs(line, asset);
    }
    fclose(file);
    finish(asset);
}

static int
definition_value(const char *name, unsigned depth)
{
    if (depth > ndefs)
        die("cyclic event alias", name);
    for (size_t i = 0; i < ndefs; i++) {
        if (strcmp(defs[i].name, name))
            continue;
        if (isdigit((unsigned char)defs[i].value[0]))
            return atoi(defs[i].value);
        return definition_value(defs[i].value, depth + 1);
    }
    die("unknown event alias", name);
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc != 3 || !realpath(argv[1], root))
        die("usage", "dice-pack SOURCE_ROOT OUTPUT_HEADER");
    output      = open_file(argv[2], "w");
    FILE *asset = begin("licenses");
    copy_path(asset, "LICENSE");
    copy_path(asset, "src/cli/LICENSE.libvsync");
    finish(asset);
    asset = begin("public");
    fputs("/*\n", asset);
    copy_path(asset, "src/cli/LICENSE.libvsync");
    fputs("*/\n", asset);
    const char *headers[] = {
        "include/dice/compiler.h",
        "deps/libvsync/include/vsync/atomic.h",
        "deps/libvsync/include/vsync/spinlock/caslock.h",
        "include/dice/types.h",
        "include/dice/log.h",
        "include/dice/pubsub.h",
        "include/dice/handler.h",
        "include/dice/interpose.h",
        "include/dice/mempool.h",
        "include/dice/now.h",
    };
    for (size_t i = 0; i < sizeof(headers) / sizeof(*headers); i++)
        expand_relative(asset, headers[i]);
    char *names[MAX_FILES];
    size_t count = list("include/dice/events", ".h", names);
    for (size_t i = 0; i < count; i++) {
        char path[PATH_MAX];
        path_join(path, "include/dice/events", names[i]);
        expand_relative(asset, path);
        free(names[i]);
    }
    expand_relative(asset, "include/dice/chains/intercept.h");
    expand_relative(asset, "include/dice/chains/capture.h");
    expand_relative(asset, "include/dice/self.h");
    finish(asset);
    asset = begin("private");
    expand_relative(asset, "deps/libvsync/include/vsync/stack/quack.h");
    finish(asset);
    source("mempool", "src/dice/mempool.c");
    source("pubsub", "src/dice/pubsub.c");
    source("pubsub-box", "src/dice/pubsub-box.c");
    source("pubsub-tweaks", "src/dice/tweaks.h");
    count = list("src/mod", ".c", names);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], "self-box.c") &&
            strcmp(names[i], "wrap_memset.c") && strcmp(names[i], "tsan.c")) {
            char path[PATH_MAX];
            path_join(path, "src/mod", names[i]);
            names[i][strlen(names[i]) - 2] = 0;
            source(names[i], path);
        }
        free(names[i]);
    }
    source("tsan", "src/mod/tsan.c.in");
    count = list("src/cli/templates", ".in", names);
    for (size_t i = 0; i < count; i++) {
        char path[PATH_MAX];
        path_join(path, "src/cli/templates", names[i]);
        names[i][strlen(names[i]) - 3] = 0;
        source(names[i], path);
        free(names[i]);
    }
    fputs(
        "static const struct asset { const char *name, *data; } assets[] = {\n",
        output);
    for (size_t i = 0; i < nassets; i++)
        fprintf(output, "{\"%s\", asset_%zu},\n", asset_names[i], i);
    fputs(
        "};\nstatic const struct standard_id { const char *name; int id; int "
        "chain; } standard_ids[] = {\n"
        "{\"ANY_EVENT\", 0, 0}, {\"CHAIN_CONTROL\", 0, 1},\n",
        output);
    for (size_t i = 0; i < ndefs; i++)
        fprintf(output, "{\"%s\", %d, %d},\n", defs[i].name,
                definition_value(defs[i].name, 0),
                strncmp(defs[i].name, "EVENT_", 6) != 0);
    fputs("};\n", output);
    if (ferror(output) || fclose(output))
        die("cannot write", argv[2]);
    return 0;
}
