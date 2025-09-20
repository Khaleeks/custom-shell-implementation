#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

/* Constants */
#define MAX_INPUT_SIZE 1024
#define MAX_TOKENS 100
#define MAX_COMMANDS 50

/* Data Structures */

/**
 * Structure to represent a single command with its arguments and redirections
 */
typedef struct {
    char **args;           // Command arguments (NULL terminated)
    char *input_file;      // Input redirection file (< file)
    char *output_file;     // Output redirection file (> file)
    char *error_file;      // Error redirection file (2> file)
    int arg_count;         // Number of arguments
} Command;

/**
 * Structure to represent a pipeline of commands
 */
typedef struct {
    Command *commands;     // Array of commands in the pipeline
    int command_count;     // Number of commands in the pipeline
} Pipeline;

/* Function prototypes from input.c */
void display_prompt(void);
char *read_input(void);

/* Function prototypes from parser.c */
Pipeline *parse_input(char *input);
int validate_pipeline(Pipeline *pipeline);

/* Function prototypes from executor.c */
int execute_pipeline(Pipeline *pipeline);
int execute_single_command(Command *cmd, int input_fd, int output_fd);
void setup_redirections(Command *cmd);

/* Function prototypes from memory.c */
void free_pipeline(Pipeline *pipeline);
void free_command(Command *cmd);

/* Function prototypes from utils.c */
void print_error(const char *message);
char **tokenize_string(char *str, const char *delim, int *token_count);

#endif /* SHELL_H */
