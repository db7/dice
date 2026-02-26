/*
 * Dice Documentation Generator
 *
 * This tool generates organized documentation for Dice framework
 * based on the structure described in the design documentation.
 *
 * It helps create categorized documentation for:
 * - Dice developers
 * - Dice module developers
 * - Users of dice modules
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

// Structure for documentation categories
typedef enum {
    DOC_DEV,
    DOC_MOD_DEV,
    DOC_USER
} doc_category_t;

// Structure for documentation sections
typedef struct {
    char* title;
    char* content;
    doc_category_t category;
} doc_section_t;

// Function to print usage information
void print_usage(const char* prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("\nOptions:\n");
    printf("  --category CAT       Category: dev|mod_dev|user\n");
    printf("  --output-dir DIR     Output directory for generated docs\n");
    printf("  --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s --category dev --output-dir ./docs\n", prog_name);
    printf("  %s --category mod_dev\n", prog_name);
    printf("  %s --category user --output-dir ./user_docs\n", prog_name);
}

// Function to create directory if it doesn't exist
int create_directory(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        return mkdir(path, 0755);
    }
    return 0;
}

// Function to generate developer documentation
void generate_dev_docs(const char* output_dir) {
    printf("Generating developer documentation...\n");

    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/dice-developer-guide.md", output_dir);

    FILE* f = fopen(filename, "w");
    if (f) {
        fprintf(f, "---\ntitle: Dice Developer Guide\n---\n\n");
        fprintf(f, "# Dice Developer Guide\n\n");
        fprintf(f, "This guide is for developers who want to contribute to or extend the Dice framework.\n\n");
        fprintf(f, "## Design Principles\n\n");
        fprintf(f, "- Low overhead and minimal performance impact\n");
        fprintf(f, "- Highly extensible through modules\n");
        fprintf(f, "- Event-driven architecture with pubsub system\n");
        fprintf(f, "- Thread safety and memory isolation\n\n");

        fprintf(f, "## Core Components\n\n");
        fprintf(f, "1. **Pubsub System**: Event distribution mechanism\n");
        fprintf(f, "2. **Memory Pool**: Isolated memory management\n");
        fprintf(f, "3. **Self Module**: Thread-local storage management\n");
        fprintf(f, "4. **Interception Modules**: Function interception and event publishing\n\n");

        fprintf(f, "## Development Workflow\n\n");
        fprintf(f, "1. Understand the pubsub architecture\n");
        fprintf(f, "2. Implement new interception modules\n");
        fprintf(f, "3. Test with existing examples\n");
        fprintf(f, "4. Submit patches for review\n\n");

        fclose(f);
        printf("  Generated: %s\n", filename);
    }
}

// Function to generate module developer documentation
void generate_mod_dev_docs(const char* output_dir) {
    printf("Generating module developer documentation...\n");

    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/dice-module-developer-guide.md", output_dir);

    FILE* f = fopen(filename, "w");
    if (f) {
        fprintf(f, "---\ntitle: Dice Module Developer Guide\n---\n\n");
        fprintf(f, "# Dice Module Developer Guide\n\n");
        fprintf(f, "This guide is for developers who want to create custom Dice modules.\n\n");
        fprintf(f, "## Module Structure\n\n");
        fprintf(f, "- Module initialization using DICE_MODULE_INIT\n");
        fprintf(f, "- Event subscription using PS_SUBSCRIBE macro\n");
        fprintf(f, "- Dispatcher functions for fast event handling\n");
        fprintf(f, "- Proper slot ordering for execution\n\n");

        fprintf(f, "## Interception Mechanism\n\n");
        fprintf(f, "- Use INTERPOSE macros for function interception\n");
        fprintf(f, "- Publish events to appropriate chains (INTERCEPT_*, CAPTURE_*)\n");
        fprintf(f, "- Handle both BEFORE/AFTER/Event variations\n");
        fprintf(f, "- Implement proper error handling and return codes\n\n");

        fprintf(f, "## Best Practices\n\n");
        fprintf(f, "1. Use DICE_MODULE_SLOT to avoid conflicts\n");
        fprintf(f, "2. Prefer CAPTURE chains over INTERCEPT chains for subscribers\n");
        fprintf(f, "3. Utilize mempool for memory allocations\n");
        fprintf(f, "4. Keep handlers lightweight and fast\n\n");

        fclose(f);
        printf("  Generated: %s\n", filename);
    }
}

// Function to generate user documentation
void generate_user_docs(const char* output_dir) {
    printf("Generating user documentation...\n");

    char filename[1024];
    snprintf(filename, sizeof(filename), "%s/dice-user-guide.md", output_dir);

    FILE* f = fopen(filename, "w");
    if (f) {
        fprintf(f, "---\ntitle: Dice User Guide\n---\n\n");
        fprintf(f, "# Dice User Guide\n\n");
        fprintf(f, "This guide is for users who want to run applications with Dice monitoring.\n\n");
        fprintf(f, "## Getting Started\n\n");
        fprintf(f, "1. Build Dice with CMake\n");
        fprintf(f, "2. Use scripts/dice wrapper or LD_PRELOAD\n");
        fprintf(f, "3. Select required modules for your use case\n\n");

        fprintf(f, "## Common Use Cases\n\n");
        fprintf(f, "- **Tracing**: Monitor program execution with detailed event logs\n");
        fprintf(f, "- **Race Detection**: Use with TSAN integration\n");
        fprintf(f, "- **Memory Analysis**: Track malloc/free patterns\n");
        fprintf(f, "- **Thread Monitoring**: Observe thread lifecycle events\n\n");

        fprintf(f, "## Usage Examples\n\n");
        fprintf(f, "```bash\n");
        fprintf(f, "# Basic tracing\n");
        fprintf(f, "scripts/dice -pthread -malloc ./my_program\n\n");
        fprintf(f, "# Race detection\n");
        fprintf(f, "scripts/dice -pthread -tsan ./my_program\n\n");
        fprintf(f, "# Custom preload\n");
        fprintf(f, "env LD_PRELOAD=libdice.so:dice-malloc.so ./my_program\n");
        fprintf(f, "```\n\n");

        fclose(f);
        printf("  Generated: %s\n", filename);
    }
}

// Parse command line arguments
int parse_arguments(int argc, char* argv[], doc_category_t* category, char** output_dir) {
    *category = DOC_DEV; // default
    *output_dir = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1; // Exit after showing help
        }
        else if (strcmp(argv[i], "--category") == 0 && i + 1 < argc) {
            if (strcmp(argv[++i], "dev") == 0) {
                *category = DOC_DEV;
            } else if (strcmp(argv[i], "mod_dev") == 0) {
                *category = DOC_MOD_DEV;
            } else if (strcmp(argv[i], "user") == 0) {
                *category = DOC_USER;
            } else {
                fprintf(stderr, "Invalid category: %s\n", argv[i]);
                return 1;
            }
        }
        else if (strcmp(argv[i], "--output-dir") == 0 && i + 1 < argc) {
            *output_dir = argv[++i];
        }
    }

    // Set default output directory
    if (!*output_dir) {
        *output_dir = ".";
    }

    return 0;
}

int main(int argc, char* argv[]) {
    doc_category_t category;
    char* output_dir;

    // Parse command line arguments
    int result = parse_arguments(argc, argv, &category, &output_dir);
    if (result == 1) {
        return 0; // Help was shown, exit normally
    }
    if (result != 0) {
        fprintf(stderr, "Error parsing arguments\n");
        return 1;
    }

    // Create output directory if needed
    create_directory(output_dir);

    // Generate documentation based on category
    int success = 0;

    switch (category) {
        case DOC_DEV:
            generate_dev_docs(output_dir);
            printf("Generated developer documentation\n");
            break;
        case DOC_MOD_DEV:
            generate_mod_dev_docs(output_dir);
            printf("Generated module developer documentation\n");
            break;
        case DOC_USER:
            generate_user_docs(output_dir);
            printf("Generated user documentation\n");
            break;
    }

    if (success == 0) {
        printf("Successfully generated documentation in %s\n", output_dir);
    } else {
        fprintf(stderr, "Failed to generate documentation\n");
        success = 1;
    }

    return success;
}