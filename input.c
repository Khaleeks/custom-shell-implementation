#include "shell.h"

/**
 * Display the shell prompt
 */
void display_prompt(void) {
    printf("$ ");
    fflush(stdout);
}

/**
 * Read input from the user
 * Returns: Dynamically allocated string containing the input, or NULL on error/EOF
 */
char *read_input(void) {
    char *input = malloc(MAX_INPUT_SIZE);
    if (!input) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    
    if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
        free(input);
        return NULL;
    }
    
    // Remove trailing newline
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    
    return input;
}
