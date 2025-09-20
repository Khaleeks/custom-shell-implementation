#include "shell.h"

/**
 * Print error message to stderr with consistent formatting
 */
void print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

/**
 * Tokenize a string based on delimiter
 * Returns: Array of tokens and sets token_count
 * Note: This is a utility function for future extensibility
 */
char **tokenize_string(char *str, const char *delim, int *token_count) {
    if (!str || !delim || !token_count) {
        return NULL;
    }
    
    char **tokens = malloc(MAX_TOKENS * sizeof(char*));
    if (!tokens) {
        perror("malloc failed");
        return NULL;
    }
    
    char *str_copy = strdup(str);
    if (!str_copy) {
        free(tokens);
        perror("strdup failed");
        return NULL;
    }
    
    char *token;
    char *saveptr;
    int count = 0;
    
    token = strtok_r(str_copy, delim, &saveptr);
    while (token != NULL && count < MAX_TOKENS - 1) {
        tokens[count] = strdup(token);
        if (!tokens[count]) {
            // Cleanup on failure
            for (int i = 0; i < count; i++) {
                free(tokens[i]);
            }
            free(tokens);
            free(str_copy);
            return NULL;
        }
        count++;
        token = strtok_r(NULL, delim, &saveptr);
    }
    
    tokens[count] = NULL;
    *token_count = count;
    
    free(str_copy);
    return tokens;
}
