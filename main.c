#include "shell.h"

/**
 * Main function - Entry point of the shell
 * Continuously reads input, parses it, and executes commands until "exit" is entered
 */
int main(void) {
    char *input;
    Pipeline *pipeline;
    
    printf("MyShell - Custom Shell Implementation\n");
    printf("Type 'exit' to quit\n\n");
    
    while (1) {
        display_prompt();
        input = read_input();
        
        // Handle EOF or read error
        if (input == NULL) {
            printf("\n");
            break;
        }
        
        // Check for exit command
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
        
        // Skip empty input
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        // Parse and execute the input
        pipeline = parse_input(input);
        if (pipeline != NULL) {
            if (validate_pipeline(pipeline)) {
                execute_pipeline(pipeline);
            }
            free_pipeline(pipeline);
        }
        
        free(input);
    }
    
    printf("Shell terminated.\n");
    return 0;
}
