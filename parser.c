#define _POSIX_C_SOURCE 200809L
#include "shell.h"
#include <glob.h>
#include <ctype.h>

/**
 * Advanced tokenizer that handles quotes and escapes
 * Properly handles single quotes, double quotes, and removes them
 * Preserves backslash sequences in double quotes for command interpretation
 */
static char **tokenize_with_quotes(char *str, int *token_count) {
    char **tokens = malloc(MAX_TOKENS * sizeof(char*));
    if (!tokens) {
        perror("malloc failed");
        return NULL;
    }
    
    int count = 0;
    char *p = str;
    
    while (*p && count < MAX_TOKENS - 1) {
        // Skip leading whitespace
        while (*p && (*p == ' ' || *p == '\t')) {
            p++;
        }
        
        if (*p == '\0') break;
        
        // Start building a token
        char token_buf[MAX_INPUT_SIZE];
        int token_len = 0;
        int in_single_quote = 0;
        int in_double_quote = 0;
        
        while (*p) {
            if (*p == '\\' && !in_single_quote && !in_double_quote && *(p+1)) {
                // Backslash escape outside quotes - consume the backslash
                p++; // Skip the backslash
                token_buf[token_len++] = *p++; // Take the next character literally
            } else if (*p == '\\' && in_double_quote && *(p+1)) {
                // Inside double quotes, preserve backslash sequences for command to interpret
                token_buf[token_len++] = *p++; // Keep the backslash
                token_buf[token_len++] = *p++; // Keep the next character
            } else if (*p == '\'' && !in_double_quote) {
                // Single quote toggle - DON'T include the quote in output
                in_single_quote = !in_single_quote;
                p++;
            } else if (*p == '"' && !in_single_quote) {
                // Double quote toggle - DON'T include the quote in output
                in_double_quote = !in_double_quote;
                p++;
            } else if ((*p == ' ' || *p == '\t') && !in_single_quote && !in_double_quote) {
                // Unquoted whitespace - end of token
                break;
            } else {
                // Regular character - include it
                token_buf[token_len++] = *p++;
            }
        }
        
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
 * Expand wildcards in a token using glob()
 * Returns array of expanded tokens or original token if no match
 */
static char **expand_wildcards(char *token, int *expanded_count) {
    // Check if token contains wildcards
    if (strchr(token, '*') == NULL && strchr(token, '?') == NULL && 
        strchr(token, '[') == NULL) {
        // No wildcards - return original token
        char **result = malloc(2 * sizeof(char*));
        result[0] = strdup(token);
        result[1] = NULL;
        *expanded_count = 1;
        return result;
    }
    
    // Perform glob expansion
    glob_t glob_result;
    memset(&glob_result, 0, sizeof(glob_result));
    
    int ret = glob(token, GLOB_NOCHECK, NULL, &glob_result);
    
    if (ret != 0) {
        // Glob failed - return original token
        char **result = malloc(2 * sizeof(char*));
        result[0] = strdup(token);
        result[1] = NULL;
        *expanded_count = 1;
        return result;
    }
    
    // If glob returned only the original pattern (no matches), return as-is
    if (glob_result.gl_pathc == 1 && strcmp(glob_result.gl_pathv[0], token) == 0) {
        char **result = malloc(2 * sizeof(char*));
        result[0] = strdup(token);
        result[1] = NULL;
        *expanded_count = 1;
        globfree(&glob_result);
        return result;
    }
    
    // Copy glob results
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
 * Parse the input string into a pipeline structure
 * Now with proper quote handling and wildcard expansion
 */
Pipeline *parse_input(char *input) {
    Pipeline *pipeline = malloc(sizeof(Pipeline));
    if (!pipeline) {
        perror("malloc failed");
        return NULL;
    }
    
    pipeline->commands = malloc(MAX_COMMANDS * sizeof(Command));
    pipeline->command_count = 0;
    
    // Check for trailing pipe
    char *input_trimmed = strdup(input);
    int len = strlen(input_trimmed);
    while (len > 0 && (input_trimmed[len-1] == ' ' || input_trimmed[len-1] == '\t')) {
        input_trimmed[--len] = '\0';
    }
    
    if (len > 0 && input_trimmed[len-1] == '|') {
        pipeline->command_count = 0;
        free(input_trimmed);
        return pipeline;
    }
    free(input_trimmed);
    
    // Check for empty pipes
    if (strstr(input, "||") != NULL) {
        pipeline->command_count = 0;
        return pipeline;
    }
    
    // Split by pipes (but respect quotes around pipes)
    char *input_copy = strdup(input);
    char *pipe_positions[MAX_COMMANDS];
    int pipe_count = 0;
    
    // Find unquoted pipe positions
    char *p = input_copy;
    int in_single = 0, in_double = 0;
    pipe_positions[pipe_count++] = p;
    
    while (*p) {
        if (*p == '\'' && !in_double) in_single = !in_single;
        else if (*p == '"' && !in_single) in_double = !in_double;
        else if (*p == '|' && !in_single && !in_double) {
            *p = '\0';
            if (*(p+1)) pipe_positions[pipe_count++] = p + 1;
        }
        p++;
    }
    
    // Process each command segment
    for (int seg = 0; seg < pipe_count && pipeline->command_count < MAX_COMMANDS; seg++) {
        Command *cmd = &pipeline->commands[pipeline->command_count];
        
        cmd->args = NULL;
        cmd->input_file = NULL;
        cmd->output_file = NULL;
        cmd->error_file = NULL;
        cmd->arg_count = 0;
        
        // Tokenize with quote handling
        int token_count = 0;
        char **tokens = tokenize_with_quotes(pipe_positions[seg], &token_count);
        
        if (token_count == 0) {
            free(tokens);
            continue;
        }
        
        // Process tokens for redirections and arguments
        int max_args = token_count * 10;  // Allow space for wildcard expansion
        cmd->args = malloc((max_args + 1) * sizeof(char*));
        int arg_index = 0;
        
        for (int i = 0; i < token_count; i++) {
            if (strcmp(tokens[i], "<") == 0) {
                if (i + 1 < token_count) {
                    cmd->input_file = strdup(tokens[++i]);
                } else {
                    cmd->input_file = strdup("");
                }
            } else if (strcmp(tokens[i], ">") == 0) {
                if (i + 1 < token_count) {
                    cmd->output_file = strdup(tokens[++i]);
                } else {
                    cmd->output_file = strdup("");
                }
            } else if (strcmp(tokens[i], "2>") == 0) {
                if (i + 1 < token_count) {
                    cmd->error_file = strdup(tokens[++i]);
                } else {
                    cmd->error_file = strdup("");
                }
            } else {
                // Regular argument - expand wildcards
                int expanded_count = 0;
                char **expanded = expand_wildcards(tokens[i], &expanded_count);
                
                for (int j = 0; j < expanded_count && arg_index < max_args; j++) {
                    cmd->args[arg_index++] = expanded[j];
                }
                free(expanded);
            }
        }
        
        cmd->args[arg_index] = NULL;
        cmd->arg_count = arg_index;
        
        // Clean up tokens
        for (int i = 0; i < token_count; i++) {
            free(tokens[i]);
        }
        free(tokens);
        
        if (cmd->arg_count > 0) {
            pipeline->command_count++;
        }
    }
    
    free(input_copy);
    return pipeline;
}

/**
 * Validate the pipeline for common errors
 */
int validate_pipeline(Pipeline *pipeline) {
    if (pipeline->command_count == 0) {
        print_error("Empty command.");
        return 0;
    }
    
    for (int i = 0; i < pipeline->command_count; i++) {
        Command *cmd = &pipeline->commands[i];
        
        if (cmd->arg_count == 0) {
            if (pipeline->command_count == 1) {
                print_error("Empty command.");
            } else {
                print_error("Empty command between pipes.");
            }
            return 0;
        }
        
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
        
        if (cmd->input_file && access(cmd->input_file, R_OK) != 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg), "cannot access '%s': No such file or directory", cmd->input_file);
            print_error(error_msg);
            return 0;
        }
    }
    
    return 1;
}
