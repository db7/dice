# C Fine-Grained Dispatcher Generator

A C-based command-line tool for generating dispatcher templates in the Dice framework with fine-grained control.

## Overview

The C Fine-Grained Dispatcher Generator is a self-contained tool that uses the system's tmplr library to generate dispatcher code. It provides advanced control over dispatcher generation by allowing different configurations for different chain-event-slot combinations.

## Building

The C version is built as part of the main Dice build process:

```bash
cmake -B build
cmake --build build
```

The tool will be installed as `bin/generate-dispatcher`.

## Usage

```bash
# Generate a main dispatcher template (traditional approach)
generate-dispatcher --type main

# Generate one file per chain
generate-dispatcher --type chain --chains 0,1,2

# Generate one file per chain/event combination
generate-dispatcher --type chain-event --chains 0,1 --events 0,1,2

# Generate one file per slot
generate-dispatcher --type slot --slots 0,1,2

# Generate to a specific output directory
generate-dispatcher --type chain --chains 0,1 --output-dir ./generated
```

## Features

- **Fine-grained control**: Generate dispatcher files at different levels
- **Self-contained**: Links directly against system tmplr library
- **Faster execution**: No process spawning overhead
- **Reliable**: No dependency on PATH or external executables
- **Flexible configuration**: Different sets of chain-event-slot combinations

## Generation Types

1. **`--type main`**: Traditional main dispatcher generation (same as before)
2. **`--type chain`**: Generate one file per chain that dispatches to events
3. **`--type chain-event`**: Generate one file per chain/event that dispatches to slots
4. **`--type slot`**: Generate one file per slot

## Module Order Resolver

In addition to the dispatcher generator, there is also a module order resolver tool:

```bash
# Process module dependencies and generate command-line arguments
generate-module-order [OPTIONS] CONFIG_FILE

# Show dependency tree
generate-module-order config.txt

# Show slot assignments
generate-module-order --slots config.txt

# Show command-line arguments for dispatcher generation
generate-module-order --cmdline config.txt
```

## Integration with Dice

The generated dispatcher code can be integrated into new Dice modules by:
1. Including the generated files in your module's build configuration
2. Using the appropriate `PS_SUBSCRIBE` macros to register handlers
3. Ensuring proper linking with the Dice core library

## Example Usage

```bash
# Generate chain-specific dispatchers for chains 0 and 1
generate-dispatcher --type chain --chains 0,1 --output-dir ./dispatchers

# Generate chain-event dispatchers for chain 0 with events 0,1,2
generate-dispatcher --type chain-event --chains 0 --events 0,1,2 --output-dir ./dispatchers

# Generate slot-specific dispatchers for slots 0,1,2
generate-dispatcher --type slot --slots 0,1,2 --output-dir ./dispatchers
```