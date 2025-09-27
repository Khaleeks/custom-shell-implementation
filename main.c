#include "shell.h"

/**
 * Main function - Entry point of the shell
 * 
 * Implements the main REPL (Read-Eval-Print-Loop) cycle:
 * 1. Display prompt and read user input
 * 2. Parse the input into commands  
 * 3. Validate and execute the commands
 * 4. Repeat until "exit" or EOF
 * 
 * The shell continues running even when individual commands fail,
 * allowing users to correct mistakes and try again.
 */
int main(void) {
    char *input;
    Pipeline *pipeline;
    
    /*
     * Display welcome message and usage instructions
     */
    printf("MyShell Custom Shell Implementation\n");
    printf("Type 'exit' to quit\n\n");
    
    /*
     * Main shell loop - continues until user exits or EOF
     */
    while (1) {
        /*
         * Get user input. display_prompt() shows "$" and read_input()
         * returns a dynamically allocated string that I must free.
         */
        display_prompt();
        input = read_input();
        
        /*
         * Handle EOF or read error (like Ctrl+D). When read_input()
         * returns NULL, it means the input stream ended or failed,
         * so I terminate the shell gracefully.
         */
        if (input == NULL) {
            printf("\n");
            break;
        }
        
        /*
         * Check for explicit exit command. strcmp() returns 0 when
         * strings match exactly.
         */
        if (strcmp(input, "exit") == 0) {
            free(input);
            break;
        }
        
        /*
         * Skip empty input lines. If user just pressed Enter,
         * there's nothing to execute.
         */
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        /*
         * Parse and execute the command pipeline.
         * 
         * parse_input() converts the raw string into a structured
         * Pipeline object. If parsing fails, it returns NULL.
         * 
         * validate_pipeline() checks for syntax errors and ensures
         * all required components are present before execution.
         * 
         * Both the Pipeline structure and input string need to be
         * freed to prevent memory leaks.
         */
        pipeline = parse_input(input);
        if (pipeline != NULL) {
            if (validate_pipeline(pipeline)) {
                execute_pipeline(pipeline);
            }
            free_pipeline(pipeline);  // Clean up parsed command structure
        }
        
        free(input);  // Clean up input string
    }
    
    /*
     * Clean termination message
     */
    printf("Shell terminated.\n");
    return 0;
}