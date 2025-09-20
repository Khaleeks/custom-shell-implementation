#include "shell.h"

/**
 * Free memory allocated for a pipeline structure
 * Recursively frees all commands and their associated memory
 */
void free_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;
    
    for (int i = 0; i < pipeline->command_count; i++) {
        free_command(&pipeline->commands[i]);
    }
    
    free(pipeline->commands);
    free(pipeline);
}

/**
 * Free memory allocated for a command structure
 * Frees all dynamically allocated strings and arrays
 */
void free_command(Command *cmd) {
    if (!cmd) return;
    
    // Free argument array
    if (cmd->args) {
        for (int i = 0; i < cmd->arg_count; i++) {
            if (cmd->args[i]) {
                free(cmd->args[i]);
            }
        }
        free(cmd->args);
    }
    
    // Free redirection file strings
    if (cmd->input_file) {
        free(cmd->input_file);
    }
    
    if (cmd->output_file) {
        free(cmd->output_file);
    }
    
    if (cmd->error_file) {
        free(cmd->error_file);
    }
    
    // Reset all pointers to NULL for safety
    cmd->args = NULL;
    cmd->input_file = NULL;
    cmd->output_file = NULL;
    cmd->error_file = NULL;
    cmd->arg_count = 0;
}
