sxp: a small C99 S-expression reader
------------------------------------

Copy sxp.c and sxp.h into another project, or build libsxp.a with make. There
are no dependencies beyond the C standard library.

    #include "sxp.h"

    struct sxp_error error;
    struct sxp *root = sxp_read(file, &error);
    if (!root) {
        fprintf(stderr, "%u: %s\n", error.line, error.message);
        return 1;
    }
    /* Walk root->child and each node's next pointer. */
    sxp_free(root);

The caller opens and closes the FILE. Parsing consumes one root expression
and requires EOF after optional whitespace/comments. Nodes preserve source
line numbers. The parser returns errors without printing or exiting and frees
partial trees on failure. It does not interpret names, numbers or application
configuration forms. See sxp.h for syntax, ownership and resource limits.

Build and test independently:

    make
    make test

Override CC, CFLAGS or LDFLAGS on the make command line as needed.

Code distributed under 0BSD license.
