#include <assert.h>

#include "state.h"

int
main(void)
{
    struct state first = {0}, second = {0}, stop = {.stop = true}, direct = {0};
    publish_constant(&first);
    assert(first.order == 12 && first.serial == 1);
    publish_dynamic(&second);
    assert(second.order == 12 && second.serial == 2);
    publish_constant(&stop);
    assert(stop.order == 1 && stop.serial == 3);
    assert(invoke_handler(&direct) == PS_OK);
    assert(direct.order == 1 && direct.serial == 4);
    return 0;
}
