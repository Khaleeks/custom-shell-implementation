#include "shell.h"

/**
 * Parse the input string into a pipeline structure
 * 
 * This function takes a raw command line string and converts it into
 * a structured Pipeline containing individual commands with their arguments
 * and file redirections. It handles pipes (|), input (<), output (>),
 * and error (2>) redirections.
 * 
 * The parsing process:
 * 1. Check for syntax errors (trailing pipes, empty pipes)
 * 2. Split input by pipe characters to get individual commands
 * 3. Parse each command for redirections and arguments
 * 4. Build the final Pipeline structure
 * 
 * Returns: Pipeline structure or NULL if parsing fails
 */
Pipeline *parse_input(char *input) {
    /*
     * Allocate memory for the main Pipeline structure.
     * This will hold all commands and metadata about the pipeline.
     */
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) {
        perror("malloc failed");
        return NULL;
    }
    
    /*
     * Allocate array to hold individual Command structures.
     * Each command in the pipeline gets its own Command entry.
     */
    pipeline->commands = malloc(MAX_COMMANDS * sizeof(Command));
    pipeline->command_count = 0;
    
    /*
     * SYNTAX ERROR CHECK: Trailing pipe detection
     * 
     * A command ending with "|" is invalid (like "ls |").
     * I create a trimmed copy to remove trailing whitespace,
     * then check if it ends with a pipe character.
     */
    char *input_trimmed = strdup(input);
    // Remove trailing whitespace
    int len = strlen(input_trimmed);
    while (len > 0 && (input_trimmed[len-1] == ' ' || input_trimmed[len-1] == '\t')) {
        input_trimmed[--len] = '\0';
    }
    
    // Check if input ends with a pipe
    if (len > 0 && input_trimmed[len-1] == '|') {
        pipeline->command_count = 0; // Mark as invalid
        free(input_trimmed);
        return pipeline; // This will trigger "Empty command" error in validation
    }
    
    free(input_trimmed);
    
    /*
     * SYNTAX ERROR CHECK: Empty pipe detection
     * 
     * Commands like "ls || grep" have empty commands between pipes.
     * I check for consecutive pipe characters which indicate this error.
     */
    char *pipe_token;
    char *pipe_saveptr;
    char *input_copy = strdup(input);
    
    if (strstr(input, "||") != NULL) {
        pipeline->command_count = 0;
        free(input_copy);
        return pipeline; // This will be caught by validation
    }
    
    /*
     * MAIN PARSING LOOP: Split by pipes
     * 
     * I use strtok_r() to split the input by "|" characters.
     * Each token represents one command in the pipeline.
     */
    pipe_token = strtok_r(input_copy, "|", &pipe_saveptr);
    
    while (pipe_token != NULL && pipeline->command_count < MAX_COMMANDS) {
        Command *cmd = &pipeline->commands[pipeline->command_count];
        
        /*
         * Initialize the Command structure with safe defaults.
         * This prevents issues with uninitialized pointers.
         */
        cmd->args = NULL;
        cmd->input_file = NULL;
        cmd->output_file = NULL;
        cmd->error_file = NULL;
        cmd->arg_count = 0;
        
        /*
         * TOKENIZE INDIVIDUAL COMMAND
         * 
         * Each command string needs to be broken into tokens (words)
         * separated by spaces or tabs. These tokens will be either
         * command arguments or redirection operators.
         */
        char *cmd_copy = strdup(pipe_token);
        char *token;
        char *saveptr;
        int token_count = 0;
        char *tokens[MAX_TOKENS];
        
        // Split command into individual tokens
        token = strtok_r(cmd_copy, " \t", &saveptr);
        while (token != NULL && token_count < MAX_TOKENS - 1) {
            tokens[token_count++] = strdup(token);
            token = strtok_r(NULL, " \t", &saveptr);
        }
        
        /*
         * Skip empty commands (just whitespace between pipes).
         * This handles cases like "ls |   | grep" where there's
         * whitespace but no actual command between pipes.
         */
        if (token_count == 0) {
            free(cmd_copy);
            pipe_token = strtok_r(NULL, "|", &pipe_saveptr);
            continue;
        }
        
        /*
         * PROCESS TOKENS: Separate arguments from redirections
         * 
         * I examine each token to determine if it's:
         * - A redirection operator (<, >, 2>)
         * - A filename following a redirection operator
         * - A regular command argument
         */
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
                // Regular argument - add to args array
                cmd->args[arg_index++] = strdup(tokens[i]);
            }
        }
        
        /*
         * Finalize the command structure:
         * - NULL-terminate the args array (required for execvp)
         * - Set the argument count
         * - Clean up temporary token storage
         */
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
 * 
 * This function performs comprehensive validation of the parsed pipeline
 * to catch syntax errors and invalid conditions before attempting execution.
 * It checks for empty commands, missing redirection files, and file accessibility.
 * 
 * The validation helps provide clear error messages to users and prevents
 * the shell from attempting to execute invalid commands.
 * 
 * Returns: 1 if valid, 0 if invalid
 */
int validate_pipeline(Pipeline *pipeline) {
    /*
     * Check for completely empty pipeline.
     * This happens when the user enters only whitespace or
     * when parsing fails due to syntax errors.
     */
    if (pipeline->command_count == 0) {
        print_error("Empty command.");
        return 0;
    }
    
    /*
     * Validate each command in the pipeline individually.
     * Each command must have at least one argument (the command name)
     * and any specified redirections must be properly formed.
     */
    for (int i = 0; i < pipeline->command_count; i++) {
        Command *cmd = &pipeline->commands[i];
        
        /*
         * Check for empty command (no arguments).
         * This can happen with malformed input like "ls | | grep"
         * where there's nothing between pipe characters.
         */
        if (cmd->arg_count == 0) {
            if (pipeline->command_count == 1) {
                print_error("Empty command.");
            } else {
                print_error("Empty command between pipes.");
            }
            return 0;
        }
        
        /*
         * Check for missing redirection files.
         * If a redirection operator was found but no filename follows,
         * the parser sets an empty string. I detect this condition here.
         */
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
        
        /*
         * Check if input file exists and is readable.
         * For input redirection, the file must exist and be accessible
         * before I attempt to execute the command. This prevents
         * confusing error messages during execution.
         */
        if (cmd->input_file && access(cmd->input_file, R_OK) != 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "cannot access '%s': No such file or directory", cmd->input_file);
            print_error(error_msg);
            return 0;
        }
    }
    
    return 1;
}
