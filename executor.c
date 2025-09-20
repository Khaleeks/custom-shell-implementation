#include "shell.h"

/**
 * Execute a pipeline of commands
 * Handles pipe creation and process coordination
 * Returns: 0 on success, -1 on failure
 */
int execute_pipeline(Pipeline *pipeline) {
    if (pipeline->command_count == 1) {
        // Single command execution
        return execute_single_command(&pipeline->commands[0], STDIN_FILENO, STDOUT_FILENO);
    }
    
    // Multi-command pipeline
    int pipes[pipeline->command_count - 1][2];
    pid_t pids[pipeline->command_count];
    
    // Create all pipes
    for (int i = 0; i < pipeline->command_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe failed");
            return -1;
        }
    }
    
    // Execute each command in the pipeline
    for (int i = 0; i < pipeline->command_count; i++) {
        pids[i] = fork();
        
        if (pids[i] == -1) {
            perror("fork failed");
            return -1;
        }
        
        if (pids[i] == 0) {
            // Child process
            
            // Set up input redirection
            if (i == 0) {
                // First command - use stdin or input file
                if (pipeline->commands[i].input_file) {
                    int fd = open(pipeline->commands[i].input_file, O_RDONLY);
                    if (fd == -1) {
                        perror("open input file failed");
                        exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDIN_FILENO);
                    close(fd);
                }
            } else {
                // Middle/last commands - use previous pipe
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            
            // Set up output redirection
            if (i == pipeline->command_count - 1) {
                // Last command - use stdout or output file
                if (pipeline->commands[i].output_file) {
                    int fd = open(pipeline->commands[i].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) {
                        perror("open output file failed");
                        exit(EXIT_FAILURE);
                    }
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
            } else {
                // First/middle commands - use next pipe
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            // Set up error redirection
            if (pipeline->commands[i].error_file) {
                int fd = open(pipeline->commands[i].error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("open error file failed");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            
            // Close all pipe file descriptors
            for (int j = 0; j < pipeline->command_count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            
            // Execute the command
            execvp(pipeline->commands[i].args[0], pipeline->commands[i].args);
            fprintf(stderr, "Command not found: %s\n", pipeline->commands[i].args[0]);
            exit(EXIT_FAILURE);
        }
    }
    
    // Parent process - close all pipes and wait for children
    for (int i = 0; i < pipeline->command_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    
    // Wait for all child processes
    for (int i = 0; i < pipeline->command_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }
    
    return 0;
}

/**
 * Execute a single command with specified input/output file descriptors
 * Note: input_fd and output_fd parameters kept for API consistency but not used
 * since single commands handle their own redirections
 * Returns: 0 on success, -1 on failure
 */
int execute_single_command(Command *cmd, int input_fd __attribute__((unused)), int output_fd __attribute__((unused))) {
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        setup_redirections(cmd);
        
        execvp(cmd->args[0], cmd->args);
        fprintf(stderr, "Command not found: %s\n", cmd->args[0]);
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        int status;
        waitpid(pid, &status, 0);
    }
    
    return 0;
}

/**
 * Set up file redirections for a command
 * Handles input (<), output (>), and error (2>) redirections
 */
void setup_redirections(Command *cmd) {
    if (cmd->input_file) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd == -1) {
            perror("open input file failed");
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }
    
    if (cmd->output_file) {
        int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open output file failed");
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    
    if (cmd->error_file) {
        int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open error file failed");
            exit(EXIT_FAILURE);
        }
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}
