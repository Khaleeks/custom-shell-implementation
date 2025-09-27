#include "shell.h"

/**
 * Print error message to stderr with consistent formatting
 * 
 * This function provides a centralized way to display error messages
 * with consistent formatting throughout the shell. All error messages
 * are prefixed with "Error: " and sent to stderr (not stdout).
 * 
 * Using stderr ensures error messages appear even when stdout is
 * redirected to a file, and the consistent format makes errors
 * easily recognizable to users.
 */
void print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

/**
 * Tokenize a string based on delimiter
 * 
 * This is a utility function that splits a string into an array of tokens
 * based on specified delimiters. It's designed for future extensibility
 * and provides robust error handling with proper memory management.
 * 
 * The function allocates memory for both the token array and individual
 * token strings, so the caller must free all returned memory.
 * 
 * Returns: Array of tokens and sets token_count, or NULL on error
 */
char **tokenize_string(char *str, const char *delim, int *token_count) {
    /*
     * Validate input parameters.
     * All parameters must be non-NULL for the function to work properly.
     */
    if (!str || !delim || !token_count) {
        return NULL;
    }
    
    /*
     * Allocate array to hold token pointers.
     * This array will contain pointers to individual token strings.
     */
    char **tokens = malloc(MAX_TOKENS * sizeof(char*));
    if (!tokens) {
        perror("malloc failed");
        return NULL;
    }
    
    /*
     * Create a copy of the input string for tokenization.
     * strtok_r() modifies the string it operates on, so I need
     * a copy to avoid modifying the original input.
     */
    char *str_copy = strdup(str);
    if (!str_copy) {
        free(tokens);
        perror("strdup failed");
        return NULL;
    }
    
    /*
     * Tokenize the string using strtok_r().
     * This function is thread-safe and allows multiple tokenizations
     * to occur simultaneously by using the saveptr parameter.
     */
    char *token;
    char *saveptr;
    int count = 0;
    
    token = strtok_r(str_copy, delim, &saveptr);
    while (token != NULL && count < MAX_TOKENS - 1) {
        /*
         * Create a copy of each token.
         * I duplicate each token string so the caller gets
         * independent copies that won't be affected when
         * I free the working copy.
         */
        tokens[count] = strdup(token);
        if (!tokens[count]) {
            /*
             * Handle memory allocation failure.
             * If strdup() fails, I need to clean up all previously
             * allocated tokens to prevent memory leaks.
             */
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
    
    /*
     * Finalize the token array.
     * NULL-terminate the array so the caller knows where it ends,
     * set the count, and clean up the working copy.
     */
    tokens[count] = NULL;
    *token_count = count;
    free(str_copy);
    
    return tokens;
}