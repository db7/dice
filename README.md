# Dice

**Dice** is a lightweight, extensible C framework for capturing and
distributing execution events in multithreaded programs. Designed for low
overhead and high flexibility, Dice enables powerful tooling for runtime
monitoring, concurrency testing, and deterministic replay using a modular
publish-subscribe (pubsub) architecture.

## Features

- Function interposition via `LD_PRELOAD`
- Pubsub-based event distribution
- Modular, pluggable architecture
- Supports thread, memory, and synchronization tracking
- Thread-local storage and memory pooling for performance

## Quickstart

Dice installs one executable, `dice`. It generates a custom runtime as two
self-contained files, `dice.c` and `dice.h`. You compile the runtime with your
own build system; there is no installed Dice library or header dependency.

```sh
cmake -S . -B build -DDICE_BUILD_LEGACY=OFF -DDICE_TESTS=OFF -DDICE_BENCHMARKS=OFF
cmake --build build
cmake --install build --prefix "$HOME/.local"
dice roll examples/roll/dice.dice --output generated
```

The configuration selects embedded interceptors and describes slots and their
event flows. Build the example runtime, plugin, and application on Linux:

```sh
cc -std=gnu11 -D_GNU_SOURCE -fPIC -shared -Igenerated \
    generated/dice.c examples/roll/observer.c -pthread -ldl -o customdice.so
cc -std=gnu11 -D_GNU_SOURCE -fPIC -shared -Igenerated \
    examples/roll/plugin.c -o plugin.so
cc examples/roll/main.c -o sample
LD_PRELOAD="$PWD/customdice.so:$PWD/plugin.so" ./sample
```

On macOS:

```sh
cc -std=gnu11 -fPIC -dynamiclib -Igenerated generated/dice.c \
    examples/roll/observer.c -pthread -o customdice.dylib
cc -std=gnu11 -fPIC -dynamiclib -undefined dynamic_lookup -Igenerated \
    examples/roll/plugin.c -o plugin.dylib
cc examples/roll/main.c -o sample
DYLD_INSERT_LIBRARIES="$PWD/customdice.dylib:$PWD/plugin.dylib" ./sample
```

Both the embedded observer and the plugin report the allocation. You can also
link the generated runtime into your application. When hosting it in an
executable, export its API symbols for plugins (for example, `-rdynamic` on
Linux).

Existing source-tree consumers can continue using `add_subdirectory(dice)` and
the `dice.h`, `dice.o`, `dice-box.o`, and `dice-*.o` targets with their existing
`DICE_MAX_TYPES`, `DICE_MAX_CHAINS`, `DICE_LAST_DISPATCH_SLOT`, and
`DICE_DISPATCH_CHAINS` settings. `DICE_BUILD_LEGACY` defaults to `ON`, preserving
the default build of the runtime and modules. Setting it to `OFF` excludes these
from the default build when tests and benchmarks are also off; the targets remain
available explicitly. Installation remains generator-only.

Event and chain headers keep their existing numeric definitions unless
`DICE_GENERATED_IDS` is defined. The generated `dice.h` defines that flag and all
resolved IDs before providing the event payload types. Downstream headers can
use the same pattern:

```c
#ifndef DICE_GENERATED_IDS
#define EVENT_REQUEST 200
#define CHAIN_APPLICATION 11
#endif

struct request_event { int value; };
```

Legacy builds use these numbers unchanged. To migrate, include generated
`dice.h` first, or force-include it in application/module compilation with
`-include /path/to/generated/dice.h`. Existing `<dice/...>` includes can remain
when Dice's source headers are on the include path. Generated mode supplies the
IDs and module macros; the downstream payload structs remain in their headers.
When force-including the header, select the module slot through compiler options
too, for example `-DDICE_MODULE_SLOT=DICE_SLOT_PROCESS`.
Declare each downstream endpoint in the configuration: generated mode does not
fall back to undeclared legacy IDs. Built-in Dice IDs and aliases retain their
existing values. Compile `dice.c` normally, and rebuild application modules and
plugins against its matching header; legacy `.o` files are not interchangeable
with generated-runtime objects. Do not link the legacy `dice.h` CMake target
into migrated targets, since it supplies legacy capacity definitions.

## Configuration

The input is a declarative S-expression with a required `(version 1)`:

```scheme
(dice
  (version 1)
  (runtime
    (embed self pthread_create malloc)
    (plugins true)
    (mempool_size 209715200))

  (slot capture
    (after dice_self)
    (before process)
    (consumes (CAPTURE_BEFORE EVENT_MALLOC))
    (produces (CHAIN_APPLICATION EVENT_REQUEST)))

  (slot process
    (consumes (CHAIN_APPLICATION EVENT_REQUEST))
    (produces (CHAIN_RESULTS EVENT_RESPONSE))))
```

Events and chains are inferred from every `consumes` and `produces` endpoint.
Their declarations are optional: use `(events (EVENT_RESPONSE 200))` or
`(chains (CHAIN_RESULTS 12))` inside `dice` to pin IDs, or `(events EVENT_UNUSED)`
to declare an otherwise unused name. An endpoint can list multiple event types:
`(consumes (CHAIN_APPLICATION EVENT_REQUEST EVENT_RESPONSE))`.

Slots receive unique positive numbers satisfying `before` and `after` constraints.
Add `(number 32)` to pin a slot. Dependency cycles, duplicate numbers, and forced
numbers that cannot satisfy the order abort generation without replacing existing
output. Event flows do not imply ordering constraints; only `before` and `after`
do. Ordering applies when handlers share a dispatched chain and event.

Implement the configured consumers in C files including the generated `dice.h`.
Select the slot using its generated macro before including the header:

```c
#define DICE_MODULE_SLOT DICE_SLOT_PROCESS
#include "dice.h"

PS_SUBSCRIBE(CHAIN_APPLICATION, EVENT_REQUEST, {
    /* Handle event; publish EVENT_RESPONSE on CHAIN_RESULTS if appropriate. */
    return PS_OK;
})
```

Compile these files together with `dice.c`. Each declared consumer has a weak
generated handler that invokes registered callbacks, or does nothing when none
are registered. A linked strong handler replaces that fallback, so its callbacks
are not also called. Undeclared direct consumers fail at compile time. `produces`
describes the topology; the C handler still performs the publication. Payload
structures stay in application headers.

Strong handlers must participate in the same link as `dice.c`. Include their
object files explicitly (CMake OBJECT targets work); a weak fallback alone will
not cause the linker to extract an implementation from a static archive. Preload
modules use callback registration through the generated header.

- `(runtime (embed ...))` selects Dice modules to put into `dice.c`. Run `dice modules`
  for the list. Select `pthread_create` alongside `self` to intercept thread
  lifecycle events. Platform restrictions of the original interceptors apply;
  `pthread_spinlock` is unavailable on macOS. When embedding `memcpy`, compile
  with `-fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset`.
- Embedded modules have slots named `dice_self`, `dice_malloc`, etc. Their event
  flows are supplied automatically. You may add `(slot dice_self (before capture))`
  or force their numbers to constrain ordering.
- `(plugins true)` is the default. Declare callback endpoints with
  `(slot monitor (callback true) (consumes (CAPTURE_EVENT EVENT_THREAD_START)))`.
  Plugins include this runtime's generated `dice.h`, select `DICE_SLOT_MONITOR`,
  and use `PS_SUBSCRIBE` or `ps_subscribe`. `(plugin true)` is an alias for
  `(callback true)`. Define `DICE_PLUGIN_MODULE` to select callback registration
  for a normally direct slot, or `DICE_DISPATCH_MODULE` to provide a strong
  implementation for a normally callback slot. Embedded modules may also select
  `(callback true)`; their callbacks are bound before runtime initialization.
  The OS loads plugins through `LD_PRELOAD` /
  `DYLD_INSERT_LIBRARIES`. Registration happens at startup, before worker threads
  publish events.
- With `(plugins false)`, the trailing callback phase is omitted. Configured
  `(callback true)` slots still work inside dispatch. If none are configured,
  callback storage is omitted entirely.
- Standard Dice IDs remain fixed. Automatic event IDs start at 128 and chain IDs
  at 7, skipping occupied IDs in alphabetical name order. Explicit IDs may use
  any unused number in 1..65534. Adding automatic names can change assignments:
  pin IDs when stability across configurations matters. All resolved IDs and
  `DICE_SLOT_*` macros appear in `dice.h`.
- Delivery has two phases. Slots through `DICE_LAST_KNOWN_SLOT` (`N`) run through
  the generated dispatcher in slot order, mixing strong handlers and callback
  fallbacks. Unknown plugin slots must be greater than `N` and run afterward in
  the existing ordered callback lists. `N` is global across chains and includes
  produces-only slots. Registrations into undeclared gaps at or below `N` are
  rejected in all builds.
- When `DICE_MODULE_SLOT` is omitted, plugins use `DICE_DEFAULT_PLUGIN_SLOT`:
  10000 or `N + 1`, whichever is larger. If `N` is `INT_MAX`, no unknown slot is
  available. Plugins can use only the generated event and chain IDs.
- `ANY_EVENT` subscriptions handle every declared event on their chain. A
  specific direct handler replaces the same-slot wildcard unless it returns
  `PS_HANDLER_OFF`. `PS_STOP_CHAIN` stops both later direct handlers and plugins.
  Callback fallbacks retain callback semantics: specific and wildcard callbacks
  both run unless stopped.
- `(include "modules/events.dice")` loads another `(dice ...)` expression,
  relative to the including file. Included files may omit `version`; define
  `runtime` in only one file. Unknown forms, duplicate IDs/endpoints, invalid
  references, and include cycles are rejected before generated files are replaced.
- Semicolons introduce comments. Strings support `\n`, `\r`, `\t`, `\\` and `\"`
  escapes. There is no evaluation, macro expansion, or executable configuration.
- Configured slots using `DICE_MODULE_INIT` also declare
  `(consumes (CHAIN_CONTROL EVENT_DICE_INIT))`.

Dispatch switches contain only explicitly subscribed event cases. Wildcards use
the default branch, so a plugin publishing a declared event does not require a
Cartesian expansion.

Generated runtimes reuse `src/dice/pubsub.c` for initialization, slot-ordered
callback registration, and delivery. Builds with `(plugins false)` use its
initialization with the dispatch-only path from `src/dice/pubsub-box.c`, which
can still invoke configured callback fallbacks. Generation supplies the
dispatcher, weak handlers, and configuration checks. Callback lists use compact
indexes when public IDs are sparse.

Runtime and plugins must use the same generated header. The header carries a
configuration/source fingerprint, checked when a plugin loads and registers.
It detects different generated configurations; it does not describe application
payload layouts or compensate for incompatible compiler options. Rebuild the
runtime and its plugins together after changing the configuration.

For test builds, compile the runtime, application modules, and plugins with
`-DDICE_CHECK_ROUTES=1`. Publications through `ps_publish` or `PS_PUBLISH` must
match the calling slot's `produces`; callback registration through `ps_subscribe`
must match the receiving slot's `consumes`. A violation terminates the process
with the slot, chain, and event in the diagnostic, even if the caller ignores
return codes. `PS_SUBSCRIBE` in a checked plugin also checks its declaration at
compile time. Static consumers are checked at compile time in all builds.

Known publishers and consumers in a checked build select their declared slot
with `DICE_MODULE_SLOT`. Unknown plugin slots above `N` may publish and consume
any declared chain/event; they have no configured routes to check.
Inside `PS_SUBSCRIBE_SLOT`, publications use that
handler's explicit slot. Helper functions outside a handler use the translation
unit's `DICE_MODULE_SLOT`. An `ANY_EVENT` endpoint allows all declared event types
on that chain; an exact consumption does not permit a wildcard subscription.

Checked builds reject calls through the raw `ps_publish` function pointer because
it carries no slot context. Use a wrapper calling `ps_publish` normally, or
`dice_publish_checked(slot, chain, type, event, md)` when passing an explicit slot.
The configuration fingerprint also rejects mixing checked and unchecked modules.
Without `DICE_CHECK_ROUTES`, publication macros call the regular API directly and
the runtime declaration checks are not compiled in.

## Inspecting a configuration

```sh
dice check system.dice
dice query system.dice
dice graph system.dice > topology.dot
dot -Tsvg topology.dot -o topology.svg
```

`query` prints resolved IDs, slots, and their consumed/produced endpoints. `graph`
emits these flows and dashed slot ordering edges in DOT format. It does not infer
publications or forwarding from arbitrary C handler code. SVG rendering uses
optional Graphviz.

`examples/roll/lotto.dice` illustrates Lotto's mutex and C++ guard modules feeding
its ingress slots, with custom events and chains inferred from endpoints. It is
a topology excerpt, not a full Lotto migration; the corresponding application
handlers still need to include `dice.h` and select their generated slots.

## Architecture Overview

Dice is composed of several modules. Its core modules include

- **Mempool**: Centralized memory manager for modules and threads.
- **Pubsub**: Event routing system based on chains and event types.
- **Self**: Manages thread-local storage and handles thread lifecycle events.

Beside these modules, Dice provice several intercept modules such as

- `pthread-create`: Hooks `pthread_create` and `pthread_join`
- `pthread-mutex`: Hooks mutex lock/unlock operations
- `pthread-cond`: Hooks condition variables
- `malloc`: Hooks `malloc`, `free`, `calloc`, `realloc`
- `cxa`: Hooks C++ guard functions (`__cxa_guard_acquire`, etc.)
- `sem`: Hooks POSIX semaphore functions
- `tsan`: Hooks thread sanitizer calls

## Use Cases

- **Execution Tracing**: Log detailed sequences of events for debugging.
- **Race Detection**: Use TSan integration to expose data races.
- **State Machine Monitoring**: Validate synchronization protocol correctness.
- **Deterministic Replay**: Control execution to replicate bugs.
- **Systematic Testing**: Explore thread interleavings to find concurrency bugs.

## Building and Installation

```sh
cmake -B build -DCMAKE_INSTALL_PREFIX=<PREFIX>
cmake --build build
cmake --install build
```

The installation prefix is `/usr/local` by default. Only `<PREFIX>/bin/dice`
is installed. The generator, S-expression parser, slot resolver, and build-time
source packer are all written in C; no Python is required. The parser lives in
`deps/sxp` as a standalone C99 library with an inline 0BSD license in both its
header and source. `libtmplr.a` is linked
into the executable and renders the code templates. Using the installed
executable requires neither build tools nor the source checkout; compiling its
generated output requires a C compiler.

Legacy libraries and the `scripts/dice` preload helper remain available inside
the source build for existing regression tests and source-tree consumers. They
are not installed. Run `ctest --test-dir build --output-on-failure`; the `roll`
test builds standalone generated runtimes and compatible/incompatible plugins.

## Further information

See examples in the `examples` directory.

- Detailed architecture notes: [doc/design.md](doc/design.md).
- Header-level API reference: [doc/api.md](doc/api.md).
- Contribution checklist (style, tests, etc):
  [doc/contributing.md](doc/contributing.md).
- Benchmark workflow: [doc/benchmarking.md](doc/benchmarking.md).
- Test layout and commands: [doc/testing.md](doc/testing.md).

---

## License

[0BSD License](LICENSE)

The generated libvsync headers use the MIT license.
Run `dice --licenses` for bundled notices. Generated headers retain their
dependency notices.
