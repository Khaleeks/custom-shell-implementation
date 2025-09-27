#include "shell.h"

/**
 * Free memory allocated for a pipeline structure
 * 
 * This function performs comprehensive cleanup of all memory associated
 * with a Pipeline structure. Since the Pipeline contains nested dynamic
 * allocations (commands containing argument arrays and file strings),
 * I need to free everything in the correct order to prevent memory leaks.
 * 
 * The cleanup hierarchy:
 * 1. Free each individual Command structure
 * 2. Free the commands array
 * 3. Free the Pipeline structure itself
 */
void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;
    
    /*
     * Free each command in the pipeline.
     * Each Command structure has its own dynamically allocated
     * memory that needs to be cleaned up individually.
     */
    for (int i = 0; i < pipeline->command_count; i++) {
        free_command(&pipeline->commands[i]);
    }
    
    /*
     * Free the array that holds all the Command structures.
     * This was allocated in parse_input() to hold MAX_COMMANDS entries.
     */
    free(pipeline->commands);
    
    /*
     * Finally, free the Pipeline structure itself.
     * This completes the cleanup of all memory associated with
     * the parsed command pipeline.
     */
    free(pipeline);
}

/**
 * Free memory allocated for a command structure
 * 
 * Each Command structure contains several dynamically allocated components:
 * - An array of argument strings (args)
 * - Individual argument strings within that array
 * - File redirection strings (input_file, output_file, error_file)
 * 
 * This function carefully frees all these components and resets pointers
 * to NULL for safety, preventing accidental use after free.
 */
void free_command(Command *cmd) {
    if (!cmd) return;
    
    /*
     * Free the argument array and all argument strings.
     * The args array contains pointers to individual argument strings,
     * so I need to free each string first, then the array itself.
     */
    if (cmd->args) {
        for (int i = 0; i < cmd->arg_count; i++) {
            if (cmd->args[i]) {
                free(cmd->args[i]);  // Free individual argument string
            }
        }
        free(cmd->args);  // Free the array of pointers
    }
    
    /*
     * Free file redirection strings.
     * These are optional - only allocated if redirections were specified
     * in the command. The if checks prevent freeing NULL pointers.
     */
    if (cmd->input_file) {
        free(cmd->input_file);
    }
    if (cmd->output_file) {
        free(cmd->output_file);
    }
    if (cmd->error_file) {
        free(cmd->error_file);
    }
    
    /*
     * Reset all pointers to NULL for safety.
     * This prevents accidental use-after-free bugs and makes
     * debugging easier by ensuring freed structures don't contain
     * dangling pointers to deallocated memory.
     */
    cmd->args = NULL;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;
    cmd->arg_count = 0;
}