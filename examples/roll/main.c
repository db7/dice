/* SPDX-License-Identifier: 0BSD */
#include <stdlib.h>

int
main(void)
{
    void *memory = malloc(1234);
    if (!memory)
        return 1;
    free(memory);
    return 0;
}
