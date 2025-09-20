#include "shell.h"

/**
 * Parse the input string into a pipeline structure
 * Handles pipes, redirections, and command arguments
 * Returns: Pipeline structure or NULL if parsing fails
 */
Pipeline *parse_input(char *input) {
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) {
        perror("malloc failed");
        return NULL;
    }
    
    pipeline->commands = malloc(MAX_COMMANDS * sizeof(Command));
    pipeline->command_count = 0;
    
    // Split input by pipes
    char *pipe_token;
    char *pipe_saveptr;
    char *input_copy = strdup(input);
    
    pipe_token = strtok_r(input_copy, "|", &pipe_saveptr);
    
    while (pipe_token != NULL && pipeline->command_count < MAX_COMMANDS) {
        Command *cmd = &pipeline->commands[pipeline->command_count];
        
        // Initialize command structure
        cmd->args = NULL;
        cmd->input_file = NULL;
        cmd->output_file = NULL;
        cmd->error_file = NULL;
        cmd->arg_count = 0;
        
        // Parse individual command for redirections and arguments
        char *cmd_copy = strdup(pipe_token);
        char *token;
        char *saveptr;
        int token_count = 0;
        char *tokens[MAX_TOKENS];
        
        // Tokenize the command
        token = strtok_r(cmd_copy, " \t", &saveptr);
        while (token != NULL && token_count < MAX_TOKENS - 1) {
            tokens[token_count++] = strdup(token);
            token = strtok_r(NULL, " \t", &saveptr);
        }
        
        // Process tokens for redirections and arguments
        int arg_index = 0;
        cmd->args = malloc((token_count + 1) * sizeof(char*));
        
        for (int i = 0; i < token_count; i++) {
            if (strcmp(tokens[i], "<") == 0) {
                // Input redirection
                if (i + 1 < token_count) {
                    cmd->input_file = strdup(tokens[++i]);
                } else {
                    // Missing input file - set empty string to trigger validation error
                    cmd->input_file = strdup("");
                }
            } else if (strcmp(tokens[i], ">") == 0) {
                // Output redirection
                if (i + 1 < token_count) {
                    cmd->output_file = strdup(tokens[++i]);
                } else {
                    // Missing output file - set empty string to trigger validation error
                    cmd->output_file = strdup("");
                }
            } else if (strcmp(tokens[i], "2>") == 0) {
                // Error redirection
                if (i + 1 < token_count) {
                    cmd->error_file = strdup(tokens[++i]);
                } else {
                    // Missing error file - set empty string to trigger validation error
                    cmd->error_file = strdup("");
                }
            } else {
                // Regular argument
                cmd->args[arg_index++] = strdup(tokens[i]);
            }
        }
        
        cmd->args[arg_index] = NULL;
        cmd->arg_count = arg_index;
        
        // Clean up tokens
        for (int i = 0; i < token_count; i++) {
            free(tokens[i]);
        }
        free(cmd_copy);
        
        pipeline->command_count++;
        pipe_token = strtok_r(NULL, "|", &pipe_saveptr);
    }
    
    free(input_copy);
    return pipeline;
}

/**
 * Validate the pipeline for common errors
 * Returns: 1 if valid, 0 if invalid
 */
int validate_pipeline(Pipeline *pipeline) {
    // Check for empty pipeline
    if (pipeline->command_count == 0) {
        print_error("Empty command.");
        return 0;
    }
    
    for (int i = 0; i < pipeline->command_count; i++) {
        Command *cmd = &pipeline->commands[i];
        
        // Check for empty command
        if (cmd->arg_count == 0) {
            if (i == 0 && pipeline->command_count == 1) {
                print_error("Empty command.");
            } else {
                print_error("Empty command between pipes.");
            }
            return 0;
        }
        
        // Check for missing redirection files (improved error detection)
        if (cmd->input_file && strlen(cmd->input_file) == 0) {
            print_error("Input file not specified.");
            return 0;
        }
        
        if (cmd->output_file && strlen(cmd->output_file) == 0) {
            print_error("Output file not specified.");
            return 0;
        }
        
        if (cmd->error_file && strlen(cmd->error_file) == 0) {
            print_error("Error output file not specified.");
            return 0;
        }
        
        // Check if input file exists and is readable
        if (cmd->input_file && access(cmd->input_file, R_OK) != 0) {
            print_error("File not found.");
            return 0;
        }
    }
    
    // Check for missing commands after pipes (improved pipe validation)
    for (int i = 0; i < pipeline->command_count - 1; i++) {
        if (pipeline->commands[i + 1].arg_count == 0) {
            print_error("Command missing after pipe.");
            return 0;
        }
    }
    
    return 1;
}
