#include "shell.h"

/**
 * Execute a pipeline of commands
 * 
 * This function is the main entry point for executing command pipelines in a shell.
 * It handles both single commands and multi-command pipelines connected by pipes.
 * 
 * A pipeline is a series of commands where the output of one command becomes the
 * input of the next command. For example: "cat file.txt | grep pattern | sort"
 * 
 * The function creates the necessary pipes, forks child processes for each command,
 * sets up proper input/output redirection, and coordinates the execution.
 * 
 * @param pipeline: Pointer to Pipeline structure containing all commands to execute
 * @return: 0 on success, -1 on failure
 */
int execute_pipeline(Pipeline *pipeline) {
    /*
     * SPECIAL CASE: Single Command Execution
     * 
     * If there's only one command in the pipeline, I don't need to create pipes
     * or coordinate multiple processes. I can simply execute the single command
     * directly using the simpler execute_single_command function.
     * 
     * This optimization avoids the overhead of pipe creation and multiple process
     * management when it's not needed.
     */
    if (pipeline->command_count == 1) {
        // Single command execution - no pipes needed
        return execute_single_command(&pipeline->commands[0], STDIN_FILENO, STDOUT_FILENO);
    }
    
    /*
     * MULTI-COMMAND PIPELINE SETUP
     * 
     * For pipelines with multiple commands, I need:
     * 1. An array of pipes to connect commands
     * 2. An array to store process IDs of child processes
     * 
     * For N commands, I need (N-1) pipes because:
     * - Command 1 output → Pipe 1 → Command 2 input
     * - Command 2 output → Pipe 2 → Command 3 input
     * - etc.
     * 
     * Each pipe has two file descriptors: [0] for reading, [1] for writing
     */
    int pipes[pipeline->command_count - 1][2];  // Array of pipes connecting commands
    pid_t pids[pipeline->command_count];        // Array to store child process IDs
    
    /*
     * PIPE CREATION PHASE
     * 
     * I create all pipes before forking any processes. This ensures all pipes
     * exist before any child process tries to use them. Each pipe() call creates
     * a communication channel with two ends: read end [0] and write end [1].
     * 
     * If any pipe creation fails, I return immediately since the pipeline
     * cannot function without proper inter-process communication channels.
     */
    for (int i = 0; i < pipeline->command_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe failed");
            return -1;
        }
    }
    
    /*
     * PROCESS CREATION AND COMMAND EXECUTION PHASE
     * 
     * Now I fork a child process for each command in the pipeline. Each child
     * will be responsible for executing one command with proper input/output
     * redirection to connect it to the pipeline.
     * 
     * The parent process manages all children and cleans up resources.
     */
    for (int i = 0; i < pipeline->command_count; i++) {
        pids[i] = fork();  // Create child process for command i
        
        if (pids[i] == -1) {
            perror("fork failed");
            return -1;
        }
        
        if (pids[i] == 0) {
            /*
             * CHILD PROCESS EXECUTION PATH
             * 
             * Each child process needs to:
             * 1. Set up its input source (stdin, file, or previous pipe)
             * 2. Set up its output destination (stdout, file, or next pipe)
             * 3. Handle error redirection if specified
             * 4. Close all unused pipe file descriptors
             * 5. Execute the assigned command
             */
            
            /*
             * INPUT REDIRECTION SETUP
             * 
             * The input source depends on the command's position in the pipeline:
             * 
             * - First command (i == 0): 
             *   Uses standard input OR reads from an input file if specified
             *   
             * - Middle/Last commands (i > 0):
             *   Reads from the output of the previous command via pipe[i-1][0]
             */
            if (i == 0) {
                // First command - check if input should come from a file
                if (pipeline->commands[i].input_file) {
                    /*
                     * Open the specified input file for reading.
                     * If the file doesn't exist or can't be opened, the command fails.
                     */
                    int fd = open(pipeline->commands[i].input_file, O_RDONLY);
                    if (fd == -1) {
                        perror("open input file failed");
                        exit(EXIT_FAILURE);
                    }
                    /*
                     * dup2() redirects stdin to read from the file instead.
                     * After this, any read from stdin will actually read from the file.
                     */
                    dup2(fd, STDIN_FILENO);
                    close(fd);  // Close original file descriptor (I have the copy)
                }
                // If no input file specified, stdin remains connected to terminal/parent
            } else {
                /*
                 * Middle or last command - input comes from previous command's output
                 * 
                 * I connect this command's stdin to the read end of the previous pipe.
                 * This creates the data flow: previous_command → pipe → this_command
                 */
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            
            /*
             * OUTPUT REDIRECTION SETUP
             * 
             * The output destination depends on the command's position:
             * 
             * - Last command (i == command_count - 1):
             *   Uses standard output OR writes to an output file if specified
             *   
             * - First/Middle commands (i < command_count - 1):
             *   Writes to the next command via pipe[i][1]
             */
            if (i == pipeline->command_count - 1) {
                // Last command - check if output should go to a file
                if (pipeline->commands[i].output_file) {
                    /*
                     * Create or truncate the output file.
                     * O_WRONLY: Open for writing only
                     * O_CREAT: Create file if it doesn't exist
                     * O_TRUNC: Truncate file to zero length if it exists
                     * 0644: File permissions (owner: read/write, group/others: read)
                     */
                    int fd = open(pipeline->commands[i].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (fd == -1) {
                        perror("open output file failed");
                        exit(EXIT_FAILURE);
                    }
                    /*
                     * Redirect stdout to write to the file.
                     * After this, any output will go to the file instead of terminal.
                     */
                    dup2(fd, STDOUT_FILENO);
                    close(fd);
                }
                // If no output file specified, stdout remains connected to terminal
            } else {
                /*
                 * First or middle command - output goes to next command via pipe
                 * 
                 * I connect this command's stdout to the write end of the current pipe.
                 * This creates the data flow: this_command → pipe → next_command
                 */
                dup2(pipes[i][1], STDOUT_FILENO);
            }
            
            /*
             * ERROR REDIRECTION SETUP
             * 
             * Error redirection is independent of the command's position in the pipeline.
             * If an error file is specified, stderr is redirected to that file.
             * Otherwise, errors go to the terminal (or wherever the parent's stderr points).
             * 
             * This allows each command to have its own error log if desired.
             */
            if (pipeline->commands[i].error_file) {
                int fd = open(pipeline->commands[i].error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd == -1) {
                    perror("open error file failed");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDERR_FILENO);  // Redirect stderr to error file
                close(fd);
            }
            
            /*
             * CLEANUP: CLOSE ALL PIPE FILE DESCRIPTORS
             * 
             * This is crucial for proper pipeline operation. Each child process
             * must close ALL pipe file descriptors, even the ones it's not using.
             * 
             * Why this is necessary:
             * 1. Prevents resource leaks (each pipe uses system file descriptors)
             * 2. Allows proper end-of-file detection in the pipeline
             * 3. Ensures commands can terminate when their input source is exhausted
             * 
             * If a child doesn't close the write end of a pipe it's reading from,
             * the read will never see EOF because there's still a potential writer.
             */
            for (int j = 0; j < pipeline->command_count - 1; j++) {
                close(pipes[j][0]);  // Close read end of pipe j
                close(pipes[j][1]);  // Close write end of pipe j
            }
            
            /*
             * COMMAND EXECUTION
             * 
             * execvp() replaces the current process image with the new command.
             * It searches for the command in the PATH environment variable.
             * 
             * args[0] is the command name, and the entire args array contains
             * the command and all its arguments, terminated by NULL.
             * 
             * If execvp() succeeds, this code never continues (process is replaced).
             * If it fails, I print an error message and exit the child process.
             */
            execvp(pipeline->commands[i].args[0], pipeline->commands[i].args);
            
            // This line only executes if execvp() failed
            fprintf(stderr, "Command not found: %s\n", pipeline->commands[i].args[0]);
            exit(EXIT_FAILURE);
        }
    }
    
    /*
     * PARENT PROCESS CLEANUP PHASE
     * 
     * The parent process (original shell) needs to:
     * 1. Close all pipe file descriptors it opened
     * 2. Wait for all child processes to complete
     * 
     * Closing pipes in the parent is essential because:
     * - The parent doesn't need the pipes for communication
     * - Leaving them open prevents proper EOF propagation in the pipeline
     * - It frees up system resources
     */
    
    // Close all pipes in the parent process
    for (int i = 0; i < pipeline->command_count - 1; i++) {
        close(pipes[i][0]);  // Close read end
        close(pipes[i][1]);  // Close write end
    }
    
    /*
     * WAIT FOR ALL CHILD PROCESSES
     * 
     * The parent must wait for all children to complete before returning.
     * This ensures:
     * 1. No zombie processes are left behind
     * 2. The shell doesn't prompt for the next command before pipeline completes
     * 3. Proper cleanup of system resources
     * 
     * waitpid() blocks until the specified child process terminates.
     * The status parameter receives the exit status, though I don't use it here.
     */
    for (int i = 0; i < pipeline->command_count; i++) {
        int status;
        waitpid(pids[i], &status, 0);  // Wait for child process pids[i]
    }
    
    return 0;  // Pipeline executed successfully
}

/**
 * Execute a single command with specified input/output file descriptors
 * 
 * This function handles the execution of a single command, which is simpler
 * than pipeline execution since no inter-process communication is needed.
 * 
 * The input_fd and output_fd parameters are kept for API consistency with
 * older versions, but are not used since single commands handle their own
 * file redirections through the Command structure.
 * 
 * @param cmd: Pointer to Command structure containing the command to execute
 * @param input_fd: (Unused) Input file descriptor for compatibility
 * @param output_fd: (Unused) Output file descriptor for compatibility
 * @return: 0 on success, -1 on failure
 */
int execute_single_command(Command *cmd, int input_fd __attribute__((unused)), int output_fd __attribute__((unused))) {
    /*
     * PROCESS CREATION
     * 
     * I fork a child process to execute the command, keeping the parent shell
     * process free to continue operation after the command completes.
     */
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }
    
    if (pid == 0) {
        /*
         * CHILD PROCESS PATH
         * 
         * The child process is responsible for:
         * 1. Setting up any file redirections specified in the command
         * 2. Executing the command with execvp()
         * 
         * If execution fails, the child exits with failure status.
         */
        
        // Set up input/output/error file redirections
        setup_redirections(cmd);
        
        // Replace process image with the command
        execvp(cmd->args[0], cmd->args);
        
        // This only executes if execvp() failed
        fprintf(stderr, "Command not found: %s\n", cmd->args[0]);
        exit(EXIT_FAILURE);
    } else {
        /*
         * PARENT PROCESS PATH
         * 
         * The parent (shell) waits for the child command to complete.
         * This ensures the shell doesn't prompt for the next command
         * until the current one finishes.
         */
        int status;
        waitpid(pid, &status, 0);  // Wait for child to complete
    }
    
    return 0;
}

/**
 * Set up file redirections for a command
 * 
 * This function handles the three types of file redirection commonly used in shells:
 * 1. Input redirection (<): Command reads from a file instead of stdin
 * 2. Output redirection (>): Command writes to a file instead of stdout  
 * 3. Error redirection (2>): Command writes errors to a file instead of stderr
 * 
 * Each redirection is independent - a command can have none, some, or all types.
 * The function modifies the calling process's file descriptors, so it should
 * only be called in child processes that are about to exec().
 * 
 * @param cmd: Pointer to Command structure containing redirection information
 */
void setup_redirections(Command *cmd) {
    /*
     * INPUT REDIRECTION SETUP
     * 
     * If an input file is specified, I redirect stdin to read from that file.
     * This makes the command read from the file as if the user had typed
     * the file contents on the keyboard.
     */
    if (cmd->input_file) {
        /*
         * Open the input file for reading only.
         * If the file doesn't exist, the open() call fails and I exit.
         */
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd == -1) {
            perror("open input file failed");
            exit(EXIT_FAILURE);
        }
        
        /*
         * dup2() duplicates the file descriptor, making STDIN_FILENO (0)
         * point to the same file as fd. After this call:
         * - Reading from stdin actually reads from the file
         * - The original stdin connection is closed
         */
        dup2(fd, STDIN_FILENO);
        close(fd);  // Close the original file descriptor (I have the duplicate)
    }
    
    /*
     * OUTPUT REDIRECTION SETUP
     * 
     * If an output file is specified, I redirect stdout to write to that file.
     * This captures all normal output from the command into the file.
     */
    if (cmd->output_file) {
        /*
         * Open/create the output file with specific flags:
         * O_WRONLY: Open for writing only
         * O_CREAT:  Create the file if it doesn't exist
         * O_TRUNC:  If file exists, truncate it to zero length (overwrite)
         * 0644:     File permissions (owner: rw-, group: r--, others: r--)
         */
        int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open output file failed");
            exit(EXIT_FAILURE);
        }
        
        /*
         * Redirect stdout to the file. After this:
         * - All printf(), puts(), etc. output goes to the file
         * - The terminal/console no longer receives the output
         */
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    
    /*
     * ERROR REDIRECTION SETUP  
     * 
     * If an error file is specified, I redirect stderr to write to that file.
     * This captures error messages, warnings, and diagnostic output from the command.
     * Error redirection is independent of output redirection.
     */
    if (cmd->error_file) {
        /*
         * Open/create the error file with the same flags as output redirection.
         * Each command can have its own error file, or multiple commands
         * can share the same error file.
         */
        int fd = open(cmd->error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open error file failed");
            exit(EXIT_FAILURE);
        }
        
        /*
         * Redirect stderr to the file. After this:
         * - All fprintf(stderr, ...), perror(), etc. output goes to the file
         * - Error messages don't appear on the terminal
         */
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
    
    /*
     * NOTE: File descriptors that aren't redirected remain unchanged.
     * For example, if only output is redirected, stdin and stderr still
     * connect to the terminal, allowing for interactive error handling.
     */
}