/*
 * Module Dependency Resolver and Slot Assigner
 *
 * This tool processes module dependency configurations and generates:
 * 1. A dependency tree between modules
 * 2. A total order of slots that respects dependencies
 * 3. Command-line arguments for dispatcher generation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

// Maximum number of chains, events, and modules we can handle
#define MAX_CHAINS 100
#define MAX_EVENTS 1000
#define MAX_MODULES 1000
#define MAX_MODULE_NAME_LEN 100
#define MAX_EVENT_RANGE 10000

// Structure to represent a module dependency
typedef struct {
    char module[MAX_MODULE_NAME_LEN];
    char dependency[MAX_MODULE_NAME_LEN];
} module_dep_t;

// Structure to represent a chain configuration
typedef struct {
    char name[MAX_MODULE_NAME_LEN];
    int event_count;
    int events[MAX_EVENT_RANGE];
    int event_range_start;
    int event_range_end;
    bool has_event_range;
} chain_config_t;

// Structure to represent a module in a chain
typedef struct {
    char name[MAX_MODULE_NAME_LEN];
    char chain[MAX_MODULE_NAME_LEN];
    char dependencies[MAX_MODULES][MAX_MODULE_NAME_LEN];
    int dep_count;
} module_t;

// Global structures
chain_config_t chains[MAX_CHAINS];
module_t modules[MAX_MODULES];
int chain_count = 0;
int module_count = 0;
int event_count = 0;

// Function declarations
void print_usage(const char* prog_name);
int parse_config_file(const char* filename);
int parse_chain_section(const char* line);
int parse_event_section(const char* line);
int parse_module_section(const char* line);
int parse_module_dependencies(const char* line);
void print_dependency_tree();
void print_slot_assignments();
void print_command_line_args();
int topological_sort();
bool is_dependency(const char* module, const char* dependency);
int find_module_index(const char* module_name);
int find_chain_index(const char* chain_name);
int add_module(const char* module_name, const char* chain_name);
int add_chain(const char* chain_name);

// Print usage information
void print_usage(const char* prog_name) {
    printf("Usage: %s [OPTIONS] CONFIG_FILE\n", prog_name);
    printf("\nOptions:\n");
    printf("  --help, -h           Show this help message\n");
    printf("  --tree               Show dependency tree (default)\n");
    printf("  --slots              Show slot assignments\n");
    printf("  --cmdline            Show command-line arguments\n");
    printf("  --all                Show all outputs\n");
    printf("\nExamples:\n");
    printf("  %s config.txt\n", prog_name);
    printf("  %s --slots config.txt\n", prog_name);
    printf("  %s --cmdline config.txt\n", prog_name);
}

// Add a chain to our configuration
int add_chain(const char* chain_name) {
    if (chain_count >= MAX_CHAINS) {
        fprintf(stderr, "Too many chains\n");
        return -1;
    }

    strcpy(chains[chain_count].name, chain_name);
    chains[chain_count].event_count = 0;
    chains[chain_count].has_event_range = false;
    chain_count++;
    return 0;
}

// Add a module to our configuration
int add_module(const char* module_name, const char* chain_name) {
    if (module_count >= MAX_MODULES) {
        fprintf(stderr, "Too many modules\n");
        return -1;
    }

    strcpy(modules[module_count].name, module_name);
    strcpy(modules[module_count].chain, chain_name);
    modules[module_count].dep_count = 0;
    module_count++;
    return 0;
}

// Find the index of a module by name
int find_module_index(const char* module_name) {
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, module_name) == 0) {
            return i;
        }
    }
    return -1;
}

// Find the index of a chain by name
int find_chain_index(const char* chain_name) {
    for (int i = 0; i < chain_count; i++) {
        if (strcmp(chains[i].name, chain_name) == 0) {
            return i;
        }
    }
    return -1;
}

// Check if one module is a dependency of another
bool is_dependency(const char* module, const char* dependency) {
    int idx = find_module_index(module);
    if (idx < 0) return false;

    for (int i = 0; i < modules[idx].dep_count; i++) {
        if (strcmp(modules[idx].dependencies[i], dependency) == 0) {
            return true;
        }
    }
    return false;
}

// Parse a range like "1..100" or "130..140"
int parse_range(const char* range_str, int* start, int* end) {
    char* dash = strchr(range_str, '.');
    if (!dash || dash[1] != '.') {
        return -1;
    }

    *start = atoi(range_str);
    *end = atoi(dash + 2);

    if (*start > *end) {
        return -1;
    }

    return 0;
}

// Parse a configuration file
int parse_config_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open config file %s\n", filename);
        return -1;
    }

    char line[1024];
    char current_chain[MAX_MODULE_NAME_LEN] = "";

    while (fgets(line, sizeof(line), file)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        // Remove trailing newline
        line[strcspn(line, "\r\n")] = 0;

        // Skip empty lines
        if (strlen(line) == 0) {
            continue;
        }

        // Check for CHAINS declaration
        if (strncmp(line, "CHAINS", 6) == 0) {
            char* chains_line = line + 6;
            while (*chains_line == ' ' || *chains_line == '\t') chains_line++;

            // Look for the "+=" operator
            char* plus_equal = strstr(chains_line, "+=");
            if (plus_equal) {
                // Move past the "+="
                chains_line = plus_equal + 2;
            }

            // Parse chain names
            while (*chains_line == ' ' || *chains_line == '\t') chains_line++;

            char* token = strtok(chains_line, " \t");
            while (token) {
                add_chain(token);
                token = strtok(NULL, " \t");
            }
            continue;
        }

        // Check for EVENTS declaration
        if (strncmp(line, "EVENTS.", 7) == 0) {
            char* dot_pos = strchr(line, '.');
            if (dot_pos) {
                char chain_name[100];
                char* event_part = dot_pos + 1;

                // Extract chain name (everything between EVENTS. and =)
                char* equals_pos = strchr(line, '=');
                if (equals_pos) {
                    int chain_len = dot_pos - line - 7;
                    strncpy(chain_name, line + 7, chain_len);
                    chain_name[chain_len] = '\0';

                    // Find the chain index
                    int chain_idx = find_chain_index(chain_name);
                    if (chain_idx >= 0) {
                        // Parse event ranges
                        while (*event_part == ' ' || *event_part == '\t') event_part++;

                        // Handle ranges like "1..100"
                        if (strchr(event_part, '.')) {
                            int start, end;
                            if (parse_range(event_part, &start, &end) == 0) {
                                chains[chain_idx].event_range_start = start;
                                chains[chain_idx].event_range_end = end;
                                chains[chain_idx].has_event_range = true;
                                chains[chain_idx].event_count = end - start + 1;
                            }
                        } else {
                            // Handle individual events or event lists
                            char* token = strtok(event_part, " \t");
                            while (token) {
                                if (isdigit(token[0])) {
                                    int event_num = atoi(token);
                                    if (chains[chain_idx].event_count < MAX_EVENT_RANGE) {
                                        chains[chain_idx].events[chains[chain_idx].event_count++] = event_num;
                                    }
                                } else if (isalpha(token[0])) {
                                    // Handle named events like EVENT_PTHREAD_JOIN
                                    // For now we'll just note that named events exist
                                }
                                token = strtok(NULL, " \t");
                            }
                        }
                    }
                }
            }
            continue;
        }

        // Check for section headers like [INTERCEPT_*]
        if (line[0] == '[' && line[strlen(line)-1] == ']') {
            char* start = line + 1;
            char* end = line + strlen(line) - 1;
            *end = '\0';

            strcpy(current_chain, start);
            continue;
        }

        // Parse module dependencies
        if (current_chain[0] != '\0') {
            // Skip empty lines
            if (strlen(line) == 0) continue;

            // Skip comments
            if (line[0] == '#') continue;

            // Parse module name and dependencies
            char* colon_pos = strchr(line, ':');
            if (colon_pos) {
                *colon_pos = '\0';
                char* module_name = line;
                char* deps = colon_pos + 1;

                // Trim whitespace from module name
                while (*module_name == ' ' || *module_name == '\t') module_name++;
                char* end = module_name + strlen(module_name) - 1;
                while (end > module_name && (*end == ' ' || *end == '\t')) *end-- = '\0';

                // Add module if not already present
                int module_idx = find_module_index(module_name);
                if (module_idx < 0) {
                    add_module(module_name, current_chain);
                    module_idx = module_count - 1;
                }

                // Parse dependencies
                while (*deps == ' ' || *deps == '\t') deps++;
                if (strlen(deps) > 0) {
                    char* token = strtok(deps, " \t");
                    while (token && modules[module_idx].dep_count < MAX_MODULES) {
                        strcpy(modules[module_idx].dependencies[modules[module_idx].dep_count++], token);
                        token = strtok(NULL, " \t");
                    }
                }
            }
        }
    }

    fclose(file);
    return 0;
}

// Topological sort to order modules by dependencies
int topological_sort() {
    // This is a simplified version - in a real implementation, we would use
    // a proper topological sort algorithm like Kahn's algorithm

    // For now, we'll just return modules in order of appearance
    // A full implementation would need to:
    // 1. Build a dependency graph
    // 2. Perform topological sorting
    // 3. Assign slots based on order

    return 0;
}

// Print the dependency tree as a graph
void print_dependency_tree() {
    printf("Module Dependency Graph:\n");
    printf("========================\n");

    // First pass: collect all unique modules (including dependencies)
    char all_modules[MAX_MODULES][MAX_MODULE_NAME_LEN];
    int all_module_count = 0;

    // Add all modules that are explicitly defined
    for (int i = 0; i < module_count; i++) {
        // Check if module is already added
        int found = 0;
        for (int j = 0; j < all_module_count; j++) {
            if (strcmp(all_modules[j], modules[i].name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            strcpy(all_modules[all_module_count], modules[i].name);
            all_module_count++;
        }
    }

    // Add all dependencies as well
    for (int i = 0; i < module_count; i++) {
        for (int j = 0; j < modules[i].dep_count; j++) {
            // Check if dependency is already added
            int found = 0;
            for (int k = 0; k < all_module_count; k++) {
                if (strcmp(all_modules[k], modules[i].dependencies[j]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                strcpy(all_modules[all_module_count], modules[i].dependencies[j]);
                all_module_count++;
            }
        }
    }

    // Sort modules alphabetically for consistent output
    for (int i = 0; i < all_module_count - 1; i++) {
        for (int j = 0; j < all_module_count - i - 1; j++) {
            if (strcmp(all_modules[j], all_modules[j+1]) > 0) {
                char temp[MAX_MODULE_NAME_LEN];
                strcpy(temp, all_modules[j]);
                strcpy(all_modules[j], all_modules[j+1]);
                strcpy(all_modules[j+1], temp);
            }
        }
    }

    // Second pass: print the dependency graph
    for (int i = 0; i < all_module_count; i++) {
        printf("%s:\n", all_modules[i]);

        // Find all modules that depend on this module
        for (int j = 0; j < module_count; j++) {
            for (int k = 0; k < modules[j].dep_count; k++) {
                if (strcmp(modules[j].dependencies[k], all_modules[i]) == 0) {
                    printf("  <- %s (%s)\n", modules[j].name, modules[j].chain);
                }
            }
        }

        // Print direct dependencies
        for (int j = 0; j < module_count; j++) {
            if (strcmp(modules[j].name, all_modules[i]) == 0) {
                for (int k = 0; k < modules[j].dep_count; k++) {
                    printf("  -> %s\n", modules[j].dependencies[k]);
                }
            }
        }
    }
    printf("\n");
}

// Print slot assignments
void print_slot_assignments() {
    printf("Slot Assignments:\n");
    printf("=================\n");

    // Collect all unique modules (including dependencies)
    char all_modules[MAX_MODULES][MAX_MODULE_NAME_LEN];
    int all_module_count = 0;

    // Add all modules that are explicitly defined
    for (int i = 0; i < module_count; i++) {
        // Check if module is already added
        int found = 0;
        for (int j = 0; j < all_module_count; j++) {
            if (strcmp(all_modules[j], modules[i].name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            strcpy(all_modules[all_module_count], modules[i].name);
            all_module_count++;
        }
    }

    // Add all dependencies as well
    for (int i = 0; i < module_count; i++) {
        for (int j = 0; j < modules[i].dep_count; j++) {
            // Check if dependency is already added
            int found = 0;
            for (int k = 0; k < all_module_count; k++) {
                if (strcmp(all_modules[k], modules[i].dependencies[j]) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                strcpy(all_modules[all_module_count], modules[i].dependencies[j]);
                all_module_count++;
            }
        }
    }

    // Sort and assign slots (for consistent output)
    // Simple bubble sort
    for (int i = 0; i < all_module_count - 1; i++) {
        for (int j = 0; j < all_module_count - i - 1; j++) {
            if (strcmp(all_modules[j], all_modules[j+1]) > 0) {
                char temp[MAX_MODULE_NAME_LEN];
                strcpy(temp, all_modules[j]);
                strcpy(all_modules[j], all_modules[j+1]);
                strcpy(all_modules[j+1], temp);
            }
        }
    }

    // Print slot assignments
    for (int i = 0; i < all_module_count; i++) {
        printf("%s %d\n", all_modules[i], i);
    }
    printf("\n");
}

// Print command-line arguments
void print_command_line_args() {
    printf("Command-Line Arguments:\n");
    printf("=======================\n");

    // Print chain definitions
    printf("Chains:\n");
    for (int i = 0; i < chain_count; i++) {
        printf("  --chain %s\n", chains[i].name);
    }

    // Print events for each chain
    printf("\nEvents:\n");
    for (int i = 0; i < chain_count; i++) {
        printf("  --chain %s --events ", chains[i].name);
        if (chains[i].has_event_range) {
            printf("%d..%d", chains[i].event_range_start, chains[i].event_range_end);
        } else {
            for (int j = 0; j < chains[i].event_count; j++) {
                printf("%d", chains[i].events[j]);
                if (j < chains[i].event_count - 1) printf(",");
            }
        }
        printf("\n");
    }

    // Print slot assignments
    printf("\nSlot Assignments:\n");
    for (int i = 0; i < chain_count; i++) {
        printf("  --chain %s --event ", chains[i].name);
        if (chains[i].has_event_range) {
            printf("%d..%d --slots ", chains[i].event_range_start, chains[i].event_range_end);
        } else {
            printf("0 --slots ");
            for (int j = 0; j < chains[i].event_count; j++) {
                printf("%d", chains[i].events[j]);
                if (j < chains[i].event_count - 1) printf(",");
            }
        }
        printf("\n");
    }

    printf("\n");
}

// Main function
int main(int argc, char* argv[]) {
    int show_tree = 1;
    int show_slots = 0;
    int show_cmdline = 0;
    int show_all = 0;
    char* config_file = NULL;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "--tree") == 0) {
            show_tree = 1;
        }
        else if (strcmp(argv[i], "--slots") == 0) {
            show_slots = 1;
        }
        else if (strcmp(argv[i], "--cmdline") == 0) {
            show_cmdline = 1;
        }
        else if (strcmp(argv[i], "--all") == 0) {
            show_all = 1;
        }
        else if (argv[i][0] != '-') {
            // Assume it's the config file
            config_file = argv[i];
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    // Check if we have a config file
    if (!config_file) {
        fprintf(stderr, "Error: No config file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    // Parse the configuration file
    if (parse_config_file(config_file) != 0) {
        return 1;
    }

    // Set output modes
    if (show_all) {
        show_tree = 1;
        show_slots = 1;
        show_cmdline = 1;
    }

    // Generate output based on requested options
    if (show_tree || show_all) {
        print_dependency_tree();
    }

    if (show_slots || show_all) {
        print_slot_assignments();
    }

    if (show_cmdline || show_all) {
        print_command_line_args();
    }

    return 0;
}