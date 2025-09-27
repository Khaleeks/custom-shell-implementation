#include "shell.h"

/**
 * Display the shell prompt
 * 
 * Shows the "$" prompt to indicate the shell is ready for user input.
 * fflush() is crucial because stdout is line-buffered - without it,
 * the prompt wouldn't appear until a newline is printed since there's
 * no '\n' after the "$" character.
 */
void display_prompt(void) {
    printf("$ ");
    fflush(stdout);  // Force immediate display of prompt
}

/**
 * Read input from the user
 * 
 * Allocates memory for user input, reads a complete line from stdin,
 * and removes the trailing newline character that fgets() includes.
 * 
 * Memory management: The returned string is dynamically allocated
 * and must be freed by the caller. Returns NULL on EOF or error.
 * 
 * Returns: Dynamically allocated string containing the input, or NULL on error/EOF
 */
char *read_input(void) {
    /*
     * Allocate memory for the input buffer. If malloc fails,
     * it's a critical error - the shell can't function without memory.
     */
    char *input = malloc(MAX_INPUT_SIZE);
    if (!input) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    
    /*
     * Read a line from stdin. fgets() returns NULL on EOF (Ctrl+D)
     * or error conditions. When this happens, I clean up the allocated
     * memory and return NULL to signal the error to the caller.
     */
    if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
        free(input);
        return NULL;
    }
    
    /*
     * Remove the trailing newline that fgets() includes.
     * This makes command processing cleaner since I don't want
     * the newline as part of the actual command string.
     */
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    
    return input;
}