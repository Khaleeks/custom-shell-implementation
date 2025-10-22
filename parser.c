/*
 * parser_advanced.c - Advanced Command Parser with Quote and Wildcard Support
 * 
 * This file implements enhanced parsing functionality that supports:
 * 1. Quote handling (single and double quotes)
 * 2. Wildcard expansion (*, ?, [...])
 * 3. Escape sequences with backslashes
 */

#define _POSIX_C_SOURCE 200809L
#include "shell.h"
#include <glob.h>
#include <ctype.h>

/**
 * Tokenize a string while respecting quotes and escape characters
 * 
 * This function splits a command string into tokens (words) while properly
 * handling quoted strings and escape sequences. Unlike simple space-based
 * splitting, this preserves spaces within quotes and handles special characters.
 * 
 * Quote behavior:
 * - Single quotes ('...'): Everything inside is treated literally
 * - Double quotes ("..."): Preserves spaces but allows escape sequences
 * - Backslash (\): Escapes the next character
 * 
 * Examples:
 *   Input:  echo "hello world"
 *   Output: ["echo", "hello world"]  (one token with space preserved)
 * 
 *   Input:  echo hello\ world
 *   Output: ["echo", "hello world"]  (escaped space)
 * 
 * @param str: Input string to tokenize
 * @param token_count: Output parameter for number of tokens found
 * @return: Array of token strings, NULL-terminated
 */
static char **tokenize_with_quotes(char *str, int *token_count) {
    char **tokens = malloc(MAX_TOKENS * sizeof(char*));
    if (!tokens) {
        perror("malloc failed");
        return NULL;
    }
    
    int count = 0;
    char *p = str;
    
    /*
     * Main tokenization loop - extracts one token at a time
     */
    while (*p && count < MAX_TOKENS - 1) {
        // Skip whitespace between tokens
        while (*p && (*p == ' ' || *p == '\t')) {
            p++;
        }
        
        if (*p == '\0') break;
        
        /*
         * Build a single token character by character
         * Track whether we're currently inside quotes
         */
        char token_buf[MAX_INPUT_SIZE];
        int token_len = 0;
        int in_single_quote = 0;
        int in_double_quote = 0;
        
        while (*p) {
            /*
             * Handle backslash escapes outside quotes
             * The backslash is consumed and the next character is taken literally
             */
            if (*p == '\\' && !in_single_quote && !in_double_quote && *(p+1)) {
                p++;  // Skip backslash
                token_buf[token_len++] = *p++;  // Add next character literally
            } 
            /*
             * Handle backslash inside double quotes
             * Preserve both backslash and next character for command interpretation
             */
            else if (*p == '\\' && in_double_quote && *(p+1)) {
                token_buf[token_len++] = *p++;  // Keep backslash
                token_buf[token_len++] = *p++;  // Keep next character
            } 
            /*
             * Handle single quote toggles
             * Quote character itself is not included in the output
             */
            else if (*p == '\'' && !in_double_quote) {
                in_single_quote = !in_single_quote;
                p++;
            } 
            /*
             * Handle double quote toggles
             * Quote character itself is not included in the output
             */
            else if (*p == '"' && !in_single_quote) {
                in_double_quote = !in_double_quote;
                p++;
            } 
            /*
             * Unquoted whitespace marks the end of a token
             */
            else if ((*p == ' ' || *p == '\t') && !in_single_quote && !in_double_quote) {
                break;
            } 
            /*
             * Regular character - add to token
             */
            else {
                token_buf[token_len++] = *p++;
            }
        }
        
        /*
         * Save completed token
         */
        if (token_len > 0) {
            token_buf[token_len] = '\0';
            tokens[count++] = strdup(token_buf);
        }
    }
    
    tokens[count] = NULL;
    *token_count = count;
    return tokens;
}

/**
 * Expand wildcard patterns in a token to matching filenames
 * 
 * This function uses the glob() system call to expand wildcard patterns
 * into lists of matching files. Supported wildcards:
 * - *: Matches zero or more characters
 * - ?: Matches exactly one character
 * - [...]: Matches one character from the set
 * 
 * Examples:
 *   *.txt     → file1.txt, file2.txt, document.txt
 *   test?.c   → test1.c, testA.c
 *   file[123] → file1, file2, file3
 * 
 * If no wildcards are present or no matches are found, the original
 * token is returned unchanged.
 * 
 * @param token: Token that may contain wildcards
 * @param expanded_count: Output parameter for number of expanded tokens
 * @return: Array of expanded tokens (or original token if no expansion)
 */
static char **expand_wildcards(char *token, int *expanded_count) {
    /*
     * Check if token contains any wildcard characters
     * If not, return original token without glob expansion
     */
    if (strchr(token, '*') == NULL && strchr(token, '?') == NULL && 
        strchr(token, '[') == NULL) {
        char **result = malloc(2 * sizeof(char*));
        result[0] = strdup(token);
        result[1] = NULL;
        *expanded_count = 1;
        return result;
    }
    
    /*
     * Perform glob expansion to find matching files
     */
    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));
    
    int ret = glob(token, GLOB_NOCHECK, NULL, &glob_result);
    
    /*
     * If glob fails, return original token
     */
    if (ret != 0) {
        char **result = malloc(2 * sizeof(char*));
        result[0] = strdup(token);
        result[1] = NULL;
        *expanded_count = 1;
        return result;
    }
    
    /*
     * If glob found no matches (returns original pattern), return token unchanged
     */
    if (glob_result.gl_pathc == 1 && strcmp(glob_result.gl_pathv[0], token) == 0) {
        char **result = malloc(2 * sizeof(char*));
        result[0] = strdup(token);
        result[1] = NULL;
        *expanded_count = 1;
        globfree(&glob_result);
        return result;
    }
    
    /*
     * Copy glob results into return array
     */
    char **result = malloc((glob_result.gl_pathc + 1) * sizeof(char*));
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        result[i] = strdup(glob_result.gl_pathv[i]);
    }
    result[glob_result.gl_pathc] = NULL;
    *expanded_count = glob_result.gl_pathc;
    
    globfree(&glob_result);
    return result;
}

/**
 * Parse input string into a pipeline structure
 * 
 * This function converts a raw command string into a structured Pipeline
 * containing parsed commands with proper quote handling and wildcard expansion.
 * 
 * The parsing process:
 * 1. Validate input for syntax errors (trailing pipes, empty commands)
 * 2. Split input by pipe characters (respecting quotes)
 * 3. Tokenize each command segment with quote handling
 * 4. Identify and process file redirections (<, >, 2>)
 * 5. Expand wildcards in arguments
 * 6. Build final Pipeline structure
 * 
 * @param input: Raw command string from user
 * @return: Pipeline structure, or NULL on allocation failure
 */
Pipeline *parse_input(char *input) {
    /*
     * Allocate and initialize pipeline structure
     */
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) {
        perror("malloc failed");
        return NULL;
    }
    
    pipeline->commands = malloc(MAX_COMMANDS * sizeof(Command));
    pipeline->command_count = 0;
    
    /*
     * Check for trailing pipe (syntax error)
     * Remove trailing whitespace first to detect actual end character
     */
    char *input_trimmed = strdup(input);
    int len = strlen(input_trimmed);
    while (len > 0 && (input_trimmed[len-1] == ' ' || input_trimmed[len-1] == '\t')) {
        input_trimmed[--len] = '\0';
    }
    
    if (len > 0 && input_trimmed[len-1] == '|') {
        pipeline->command_count = 0;  // Mark as invalid
        free(input_trimmed);
        return pipeline;
    }
    free(input_trimmed);
    
    /*
     * Check for consecutive pipes (syntax error)
     */
    if (strstr(input, "||") != NULL) {
        pipeline->command_count = 0;
        return pipeline;
    }
    
    /*
     * Split input by unquoted pipe characters
     * This respects quotes so pipes inside quotes are not treated as separators
     */
    char *input_copy = strdup(input);
    char *pipe_positions[MAX_COMMANDS];
    int pipe_count = 0;
    
    /*
     * Walk through input, tracking quote state and finding unquoted pipes
     */
    char *p = input_copy;
    int in_single = 0, in_double = 0;
    pipe_positions[pipe_count++] = p;
    
    while (*p) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '|' && !in_single && !in_double) {
            *p = '\0';  // Null-terminate this segment
            if (*(p+1)) pipe_positions[pipe_count++] = p + 1;
        }
        p++;
    }
    
    /*
     * Process each command segment between pipes
     */
    for (int seg = 0; seg < pipe_count && pipeline->command_count < MAX_COMMANDS; seg++) {
        Command *cmd = &pipeline->commands[pipeline->command_count];
        
        /*
         * Initialize command structure
         */
        cmd->args = NULL;
        cmd->input_file = NULL;
        cmd->output_file = NULL;
        cmd->error_file = NULL;
        cmd->arg_count = 0;
        
        /*
         * Tokenize this command segment with quote handling
         */
        int token_count = 0;
        char **tokens = tokenize_with_quotes(pipe_positions[seg], &token_count);
        
        if (token_count == 0) {
            free(tokens);
            continue;
        }
        
        /*
         * Process tokens to separate arguments from redirections
         * Allocate extra space to accommodate wildcard expansion
         */
        int max_args = token_count * 10;
        cmd->args = malloc((max_args + 1) * sizeof(char*));
        int arg_index = 0;
        
        for (int i = 0; i < token_count; i++) {
            /*
             * Handle input redirection operator
             */
            if (strcmp(tokens[i], "<") == 0) {
                if (i + 1 < token_count) {
                    cmd->input_file = strdup(tokens[++i]);
                } else {
                    cmd->input_file = strdup("");  // Missing filename triggers validation error
                }
            } 
            /*
             * Handle output redirection operator
             */
            else if (strcmp(tokens[i], ">") == 0) {
                if (i + 1 < token_count) {
                    cmd->output_file = strdup(tokens[++i]);
                } else {
                    cmd->output_file = strdup("");
                }
            } 
            /*
             * Handle error redirection operator
             */
            else if (strcmp(tokens[i], "2>") == 0) {
                if (i + 1 < token_count) {
                    cmd->error_file = strdup(tokens[++i]);
                } else {
                    cmd->error_file = strdup("");
                }
            } 
            /*
             * Regular argument - expand wildcards and add to args array
             */
            else {
                int expanded_count = 0;
                char **expanded = expand_wildcards(tokens[i], &expanded_count);
                
                for (int j = 0; j < expanded_count && arg_index < max_args; j++) {
                    cmd->args[arg_index++] = expanded[j];
                }
                free(expanded);
            }
        }
        
        /*
         * Finalize command structure
         */
        cmd->args[arg_index] = NULL;
        cmd->arg_count = arg_index;
        
        /*
         * Clean up temporary token array
         */
        for (int i = 0; i < token_count; i++) {
            free(tokens[i]);
        }
        free(tokens);
        
        /*
         * Only add command if it has arguments
         */
        if (cmd->arg_count > 0) {
            pipeline->command_count++;
        }
    }
    
    free(input_copy);
    return pipeline;
}

/**
 * Validate pipeline for common errors
 * 
 * This function checks the parsed pipeline for syntax errors and invalid
 * conditions before execution. It verifies:
 * - Pipeline is not empty
 * - Each command has at least one argument
 * - Redirection operators have associated filenames
 * - Input files exist and are accessible
 * 
 * @param pipeline: Pipeline structure to validate
 * @return: 1 if valid, 0 if invalid
 */
int validate_pipeline(Pipeline *pipeline) {
    /*
     * Check for empty pipeline
     */
    if (pipeline->command_count == 0) {
        print_error("Empty command.");
        return 0;
    }
    
    /*
     * Validate each command in the pipeline
     */
    for (int i = 0; i < pipeline->command_count; i++) {
        Command *cmd = &pipeline->commands[i];
        
        /*
         * Check for empty command (no arguments)
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
         * Verify input redirection has a filename
         */
        if (cmd->input_file && strlen(cmd->input_file) == 0) {
            print_error("Input file not specified.");
            return 0;
        }
        
        /*
         * Verify output redirection has a filename
         */
        if (cmd->output_file && strlen(cmd->output_file) == 0) {
            print_error("Output file not specified.");
            return 0;
        }
        
        /*
         * Verify error redirection has a filename
         */
        if (cmd->error_file && strlen(cmd->error_file) == 0) {
            print_error("Error output file not specified.");
            return 0;
        }
        
        /*
         * Check if input file exists and is readable
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