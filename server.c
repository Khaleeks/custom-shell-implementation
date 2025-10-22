/*
 * server.c - Network Shell Server Implementation
 * 
 * This file implements a network-based shell server that allows remote clients
 * to connect via TCP/IP and execute shell commands. The server receives commands
 * over the network, executes them using the shell implementation from other files,
 * captures their output, and sends the results back to the client.
 * 
 * Key Features:
 * - TCP server that listens on port 8080
 * - Handles one client at a time (sequential, not concurrent)
 * - Captures both stdout and stderr from executed commands
 * - Provides detailed logging of all operations
 * - Handles edge cases like empty output and errors gracefully
 */

#include "shell.h"           // Shell implementation (parsing, execution, etc.)
#include <sys/socket.h>      // Socket functions: socket(), bind(), listen(), accept()
#include <netinet/in.h>      // Internet address structures: sockaddr_in
#include <arpa/inet.h>       // Internet operations: inet_ntoa()

/*
 * CONFIGURATION CONSTANTS
 * 
 * These define the behavior and capacity of the server
 */
#define PORT 8080                      // TCP port the server listens on
#define BUFFER_SIZE 4096               // Maximum size for command output (4KB)
#define EMPTY_RESPONSE_MARKER "\x01"  // Special marker byte sent when command produces no output
                                       // Using \x01 (ASCII SOH - Start of Header) which is unlikely
                                       // to appear in normal command output

/**
 * ============================================================================
 * FUNCTION: capture_command_output
 * ============================================================================
 * 
 * PURPOSE:
 * This function executes a shell command pipeline and captures both its
 * standard output (stdout) and standard error (stderr) into separate buffers.
 * 
 * WHY THIS IS NEEDED:
 * Normally when you run a command, its output goes directly to the terminal.
 * For a network server, we need to capture that output so we can send it
 * back to the remote client over the network connection.
 * 
 * HOW IT WORKS:
 * 1. Creates two pipes (one for stdout, one for stderr)
 * 2. Forks a child process to run the command
 * 3. Child redirects its output to the pipes and executes the command
 * 4. Parent reads from the pipes to capture all output
 * 5. Returns the captured output in the provided buffers
 * 
 * PARAMETERS:
 * @param pipeline:     The parsed command(s) to execute
 * @param output_buf:   Buffer to store captured stdout (normal output)
 * @param error_buf:    Buffer to store captured stderr (error messages)
 * @param output_size:  Size of the output buffer
 * @param error_size:   Size of the error buffer
 * 
 * RETURNS:
 * 0 on success, -1 on failure
 */
int capture_command_output(Pipeline *pipeline, char *output_buf, char *error_buf, 
                          size_t output_size, size_t error_size) {
    
    /*
     * STEP 1: CREATE PIPES FOR OUTPUT CAPTURE
     * 
     * A pipe is a one-way communication channel with two ends:
     * - pipe[0]: Reading end (where data comes out)
     * - pipe[1]: Writing end (where data goes in)
     * 
     * We need two separate pipes:
     * - stdout_pipe: To capture normal command output
     * - stderr_pipe: To capture error messages
     * 
     * Think of pipes like tubes: you write into one end, and can read
     * from the other end. Perfect for capturing output!
     */
    int stdout_pipe[2];  // Pipe for capturing stdout
    int stderr_pipe[2];  // Pipe for capturing stderr
    
    /*
     * Initialize buffers to empty strings.
     * This ensures if nothing is captured, the buffers contain valid
     * empty C strings rather than garbage data.
     */
    output_buf[0] = '\0';
    error_buf[0] = '\0';
    
    /*
     * Create both pipes. If either fails, we can't capture output,
     * so we return an error immediately.
     */
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        perror("pipe failed");
        return -1;
    }
    
    /*
     * STEP 2: FORK A CHILD PROCESS
     * 
     * fork() creates an exact copy of the current process:
     * - The child process (returns 0) will execute the command
     * - The parent process (returns child's PID) will capture the output
     * 
     * Why fork? Because we need two separate processes:
     * 1. One to run the command (and have its output redirected to pipes)
     * 2. One to read from those pipes (can't read and write at same time)
     */
    pid_t pid = fork();
    
    /*
     * Error check: If fork() fails (returns -1), we're out of resources.
     * Clean up the pipes we created and return error.
     */
    if (pid == -1) {
        perror("fork failed");
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }
    
    /*
     * ========================================================================
     * CHILD PROCESS EXECUTION PATH (pid == 0)
     * ========================================================================
     * 
     * The child's job is simple:
     * 1. Redirect stdout and stderr to the write ends of the pipes
     * 2. Execute the command
     * 3. Exit when done
     * 
     * After redirection, anything the command prints goes into the pipes
     * instead of the terminal.
     */
    if (pid == 0) {
        /*
         * Close the reading ends of both pipes in the child.
         * The child only needs to WRITE to the pipes, not read from them.
         * Closing unused file descriptors is good practice and prevents
         * pipe deadlocks.
         */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        /*
         * REDIRECT OUTPUT TO PIPES
         * 
         * dup2() is the key system call here. It duplicates a file descriptor,
         * making two file descriptors point to the same file/pipe.
         * 
         * dup2(stdout_pipe[1], STDOUT_FILENO) means:
         * "Make file descriptor 1 (stdout) point to the same place as
         *  stdout_pipe[1] (the write end of the stdout pipe)"
         * 
         * After this, when the command does printf() or any stdout write,
         * the data goes into the pipe instead of the terminal!
         */
        dup2(stdout_pipe[1], STDOUT_FILENO);  // Redirect stdout → stdout pipe
        dup2(stderr_pipe[1], STDERR_FILENO);  // Redirect stderr → stderr pipe
        
        /*
         * Close the original pipe file descriptors.
         * After dup2(), we have duplicates at STDOUT_FILENO and STDERR_FILENO,
         * so we don't need the originals anymore.
         */
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        /*
         * Execute the command pipeline.
         * This function (from executor.c) will run the command(s).
         * All output goes into our pipes because we redirected stdout/stderr.
         */
        execute_pipeline(pipeline);
        
        /*
         * Exit the child process when command completes.
         * The child is done - no need to continue running.
         */
        exit(EXIT_SUCCESS);
    } 
    /*
     * ========================================================================
     * PARENT PROCESS EXECUTION PATH (pid > 0)
     * ========================================================================
     * 
     * The parent's job is to:
     * 1. Read all output from the pipes
     * 2. Store it in the provided buffers
     * 3. Wait for the child to finish
     * 
     * The parent READS from the pipes while the child WRITES to them.
     */
    else {
        /*
         * Close the writing ends of both pipes in the parent.
         * The parent only needs to READ from the pipes, not write to them.
         * This is crucial: if the parent leaves the write end open, the
         * read() calls below might never see EOF!
         */
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        /*
         * READ STDOUT FROM THE PIPE
         * 
         * We read in a loop because the command might produce more output
         * than can be read in a single read() call. We keep reading until:
         * - read() returns 0 (EOF - child closed the write end)
         * - read() returns -1 (error)
         * - We've filled our buffer
         */
        ssize_t bytes_read = 0;     // Bytes read in current iteration
        ssize_t total_read = 0;     // Total bytes read so far
        
        /*
         * Read loop for stdout:
         * - read() returns the number of bytes actually read
         * - We add to total_read to track our position in the buffer
         * - We subtract 1 from buffer size to leave room for null terminator
         */
        while ((bytes_read = read(stdout_pipe[0], output_buf + total_read, 
                                 output_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            
            // Stop if buffer is almost full (leave room for null terminator)
            if (total_read >= (ssize_t)output_size - 1) break;
        }
        
        /*
         * Null-terminate the output string.
         * This makes output_buf a valid C string that can be used with
         * string functions like strlen(), printf(), etc.
         */
        output_buf[total_read] = '\0';
        
        /*
         * READ STDERR FROM THE PIPE
         * 
         * Same process as stdout, but reading from the stderr pipe.
         * Error messages and warnings from the command end up here.
         */
        total_read = 0;  // Reset counter for stderr
        while ((bytes_read = read(stderr_pipe[0], error_buf + total_read, 
                                 error_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            if (total_read >= (ssize_t)error_size - 1) break;
        }
        error_buf[total_read] = '\0';
        
        /*
         * Close the reading ends of the pipes.
         * We're done reading, so close them to free up resources.
         */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        /*
         * WAIT FOR CHILD TO COMPLETE
         * 
         * waitpid() blocks until the child process terminates.
         * This is important because:
         * 1. Prevents zombie processes (child processes that finished but
         *    whose exit status hasn't been collected)
         * 2. Ensures command has fully completed before we return
         * 3. Allows us to get the exit status (though we don't use it here)
         */
        int status;
        waitpid(pid, &status, 0);
    }
    
    return 0;  // Success!
}

/**
 * ============================================================================
 * FUNCTION: handle_client
 * ============================================================================
 * 
 * PURPOSE:
 * This function handles all communication with a single connected client.
 * It runs in a loop, receiving commands from the client, executing them,
 * and sending back the results.
 * 
 * LIFECYCLE:
 * 1. Client connects
 * 2. Loop: Receive command → Execute → Send results
 * 3. Continue until client sends "exit" or disconnects
 * 4. Close connection
 * 
 * WHY THE LOOP:
 * We want to handle multiple commands from the same client without requiring
 * them to reconnect each time. The loop keeps the connection alive and
 * processes commands one at a time.
 * 
 * PARAMETERS:
 * @param client_socket: File descriptor for the connected client's socket
 */
void handle_client(int client_socket) {
    /*
     * DECLARE BUFFERS FOR COMMUNICATION
     * 
     * We need three buffers:
     * 1. command_buffer: Stores the command received from client
     * 2. output_buffer: Stores stdout from command execution
     * 3. error_buffer: Stores stderr from command execution
     */
    char command_buffer[MAX_INPUT_SIZE];  // Command received from client
    char output_buffer[BUFFER_SIZE];      // Command's stdout output
    char error_buffer[BUFFER_SIZE];       // Command's stderr output
    
    printf("[INFO] Client connected.\n");
    
    /*
     * MAIN CLIENT COMMUNICATION LOOP
     * 
     * This loop handles the entire client session:
     * - Receive commands from the client
     * - Execute them
     * - Send results back
     * - Repeat until client disconnects or sends "exit"
     */
    while (1) {
        /*
         * Clear all buffers before each iteration.
         * memset() fills the buffer with zeros, ensuring no leftover
         * data from previous commands remains.
         */
        memset(command_buffer, 0, MAX_INPUT_SIZE);
        memset(output_buffer, 0, BUFFER_SIZE);
        memset(error_buffer, 0, BUFFER_SIZE);
        
        /*
         * RECEIVE COMMAND FROM CLIENT
         * 
         * recv() reads data from the network socket.
         * - client_socket: Which client to read from
         * - command_buffer: Where to store the received data
         * - MAX_INPUT_SIZE - 1: Maximum bytes to read (leave room for \0)
         * - 0: Flags (none in this case)
         * 
         * recv() returns:
         * - Positive number: Number of bytes received
         * - 0: Client closed the connection gracefully
         * - Negative: Error occurred
         */
        ssize_t bytes_received = recv(client_socket, command_buffer, 
                                     MAX_INPUT_SIZE - 1, 0);
        
        /*
         * Check if client disconnected or error occurred.
         * If recv() returns 0 or negative, the connection is broken.
         */
        if (bytes_received <= 0) {
            printf("[INFO] Client disconnected.\n");
            break;  // Exit the loop, ending the client session
        }
        
        /*
         * Null-terminate the received command.
         * Network data isn't automatically null-terminated, so we must
         * add \0 to make it a valid C string.
         */
        command_buffer[bytes_received] = '\0';
        
        /*
         * Remove trailing newline if present.
         * Clients might send commands with \n at the end (like pressing Enter).
         * We remove it to clean up the command string.
         */
        size_t len = strlen(command_buffer);
        if (len > 0 && command_buffer[len - 1] == '\n') {
            command_buffer[len - 1] = '\0';
        }
        
        /*
         * Log the received command for debugging/monitoring
         */
        printf("[RECEIVED] Received command: \"%s\" from client.\n", command_buffer);
        
        /*
         * CHECK FOR EXIT COMMAND
         * 
         * If client sends "exit", they want to disconnect.
         * We send a termination message and break out of the loop.
         */
        if (strcmp(command_buffer, "exit") == 0) {
            printf("[INFO] Client requested exit.\n");
            const char *exit_msg = "Shell terminated.\n";
            send(client_socket, exit_msg, strlen(exit_msg), 0);
            break;  // Exit loop, client session ends
        }
        
        /*
         * SKIP EMPTY COMMANDS
         * 
         * If the command is empty (just whitespace or nothing),
         * there's nothing to execute. Continue to next iteration.
         */
        if (strlen(command_buffer) == 0) {
            continue;
        }
        
        printf("[EXECUTING] Executing command: \"%s\"\n", command_buffer);
        
        /*
         * PARSE THE COMMAND
         * 
         * parse_input() (from parser.c) converts the command string into
         * a Pipeline structure that can be executed. It handles:
         * - Breaking the command into tokens
         * - Identifying pipes (|)
         * - Parsing redirections (<, >, 2>)
         * 
         * Returns NULL if parsing fails (syntax error).
         */
        Pipeline *pipeline = parse_input(command_buffer);
        
        /*
         * Check if parsing failed
         */
        if (pipeline == NULL) {
            const char *error = "[ERROR] Failed to parse command.\n";
            printf("%s", error);
            
            // Send error message back to client
            snprintf(output_buffer, BUFFER_SIZE, "Error: Failed to parse command.\n");
            printf("[OUTPUT] Sending error message to client.\n");
            send(client_socket, output_buffer, strlen(output_buffer), 0);
            continue;  // Skip to next command
        }
        
        /*
         * VALIDATE THE PARSED PIPELINE
         * 
         * validate_pipeline() (from parser.c) checks for common errors:
         * - Empty commands
         * - Missing redirection files
         * - Invalid syntax
         * 
         * Returns 1 if valid, 0 if invalid.
         */
        if (!validate_pipeline(pipeline)) {
            // Validation failed - send error to client
            snprintf(output_buffer, BUFFER_SIZE, "Error: Invalid command syntax.\n");
            printf("[OUTPUT] Sending error message to client.\n");
            send(client_socket, output_buffer, strlen(output_buffer), 0);
            
            // Clean up the pipeline structure before continuing
            free_pipeline(pipeline);
            continue;
        }
        
        /*
         * EXECUTE THE COMMAND AND CAPTURE OUTPUT
         * 
         * capture_command_output() runs the command and stores:
         * - stdout in output_buffer
         * - stderr in error_buffer
         * 
         * Returns 0 on success, -1 on failure.
         */
        int exec_result = capture_command_output(pipeline, output_buffer, 
                                                error_buffer, BUFFER_SIZE, BUFFER_SIZE);
        
        /*
         * Free the pipeline structure now that we're done executing.
         * This prevents memory leaks by cleaning up all dynamically
         * allocated memory used for the parsed command.
         */
        free_pipeline(pipeline);
        
        /*
         * Check if execution failed
         */
        if (exec_result == -1) {
            const char *error = "[ERROR] Failed to execute command.\n";
            printf("%s", error);
            snprintf(output_buffer, BUFFER_SIZE, "Error: Failed to execute command.\n");
            printf("[OUTPUT] Sending error message to client.\n");
            send(client_socket, output_buffer, strlen(output_buffer), 0);
            continue;
        }
        
        /*
         * PREPARE RESPONSE TO SEND BACK TO CLIENT
         * 
         * We need to decide what to send based on what the command produced:
         * 1. If there's error output (stderr), send that (errors are priority)
         * 2. If there's normal output (stdout), send that
         * 3. If there's no output at all, send a special marker
         * 
         * The response buffer is twice the size of output_buffer to handle
         * cases where we might want to combine both stdout and stderr.
         */
        char response[BUFFER_SIZE * 2];
        memset(response, 0, sizeof(response));
        
        /*
         * CASE 1: Command produced error output
         * 
         * If stderr has content, something went wrong or the command
         * printed warnings. We prioritize sending this to the client.
         */
        if (strlen(error_buffer) > 0) {
            printf("[ERROR] Command produced error output:\n%s", error_buffer);
            printf("[OUTPUT] Sending error message to client.\n");
            snprintf(response, sizeof(response), "%s", error_buffer);
        } 
        /*
         * CASE 2: Command produced normal output
         * 
         * If stdout has content, the command executed successfully
         * and produced output. Send this to the client.
         */
        else if (strlen(output_buffer) > 0) {
            printf("[OUTPUT] Sending output to client:\n%s", output_buffer);
            snprintf(response, sizeof(response), "%s", output_buffer);
        } 
        /*
         * CASE 3: Command produced no output
         * 
         * Some commands execute successfully but produce no output
         * (like 'mkdir newdir' or 'touch file.txt'). We need to send
         * SOMETHING to let the client know the command completed.
         * 
         * We use a special marker (EMPTY_RESPONSE_MARKER) which is
         * a non-printable character (\x01) that the client can recognize.
         */
        else {
            printf("[OUTPUT] Command executed successfully with no output.\n");
            snprintf(response, sizeof(response), "%s", EMPTY_RESPONSE_MARKER);
        }
        
        /*
         * SEND RESPONSE TO CLIENT
         * 
         * send() writes data to the network socket.
         * - client_socket: Which client to send to
         * - response: The data to send
         * - strlen(response): How many bytes to send
         * - 0: Flags (none)
         * 
         * send() returns the number of bytes actually sent, or -1 on error.
         */
        ssize_t sent = send(client_socket, response, strlen(response), 0);
        if (sent == -1) {
            perror("Send failed");
            break;  // Can't send to client, connection broken
        }
        
        /*
         * Loop continues - ready to receive the next command from client
         */
    }
    
    /*
     * CLIENT SESSION ENDED
     * 
     * Close the client socket to free up the network connection.
     * The client has either disconnected or sent "exit".
     */
    close(client_socket);
}

/**
 * ============================================================================
 * FUNCTION: main
 * ============================================================================
 * 
 * PURPOSE:
 * This is the server's main function. It sets up a TCP server socket,
 * listens for incoming client connections, and handles each client
 * sequentially (one at a time).
 * 
 * SERVER LIFECYCLE:
 * 1. Create server socket
 * 2. Bind socket to port 8080
 * 3. Listen for connections
 * 4. Loop forever: Accept client → Handle client → Repeat
 * 
 * NETWORK CONCEPTS:
 * - Socket: An endpoint for network communication (like a telephone)
 * - Bind: Associate the socket with a specific port number
 * - Listen: Tell OS to accept incoming connections on this socket
 * - Accept: Wait for a client to connect, return a new socket for that client
 */
int main(void) {
    /*
     * DECLARE SOCKET VARIABLES
     * 
     * - server_socket: The listening socket that accepts new connections
     * - client_socket: A socket for communicating with a connected client
     * - server_addr: Structure holding server's address info (IP, port)
     * - client_addr: Structure holding client's address info
     * - client_addr_len: Size of client address structure
     */
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    /*
     * STEP 1: CREATE THE SERVER SOCKET
     * 
     * socket() creates a new socket (endpoint for communication).
     * 
     * Parameters:
     * - AF_INET: Use IPv4 addresses (like 192.168.1.1)
     * - SOCK_STREAM: Use TCP (reliable, ordered, connection-based)
     * - 0: Let OS choose the appropriate protocol (TCP for SOCK_STREAM)
     * 
     * Returns: File descriptor for the socket, or -1 on error
     * 
     * Think of this like getting a phone line installed in your building.
     */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    /*
     * STEP 2: SET SOCKET OPTIONS
     * 
     * SO_REUSEADDR allows the server to restart immediately even if
     * the port is in a "TIME_WAIT" state from a previous run.
     * 
     * Without this, if you stop and quickly restart the server, you might
     * get "Address already in use" error because the OS hasn't fully
     * released the port yet.
     * 
     * Parameters to setsockopt():
     * - server_socket: Which socket to configure
     * - SOL_SOCKET: Socket-level option (not protocol-specific)
     * - SO_REUSEADDR: The specific option to set
     * - &opt: Pointer to the option value (1 = enable)
     * - sizeof(opt): Size of the option value
     */
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    /*
     * STEP 3: CONFIGURE SERVER ADDRESS
     * 
     * The sockaddr_in structure tells the OS what address and port
     * we want to bind our server to.
     * 
     * Think of this like writing your building's address on the phone line.
     */
    memset(&server_addr, 0, sizeof(server_addr));  // Zero out the structure
    
    server_addr.sin_family = AF_INET;              // Use IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;      // Accept connections on any network interface
                                                     // (0.0.0.0 - listen on all IPs of this machine)
    server_addr.sin_port = htons(PORT);            // Port 8080
                                                     // htons() converts from host byte order to
                                                     // network byte order (handles endianness)
    
    /*
     * STEP 4: BIND SOCKET TO ADDRESS
     * 
     * bind() associates the socket with the address and port.
     * After binding, the OS knows that network traffic for port 8080
     * should be directed to this socket.
     * 
     * Parameters:
     * - server_socket: The socket to bind
     * - (struct sockaddr *)&server_addr: Address to bind to (cast to generic sockaddr)
     * - sizeof(server_addr): Size of the address structure
     * 
     * Think of this like officially registering your phone number.
     */
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    /*
     * STEP 5: START LISTENING FOR CONNECTIONS
     * 
     * listen() marks the socket as passive - it will be used to accept
     * incoming connections rather than initiate them.
     * 
     * Parameters:
     * - server_socket: The socket to listen on
     * - 5: Backlog - maximum number of pending connections to queue
     *      If 5 clients try to connect simultaneously, they'll all succeed
     *      (queued). The 6th client will fail or have to wait.
     * 
     * Think of this like turning on your phone and waiting for calls.
     */
    if (listen(server_socket, 5) == -1) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    /*
     * Server is now ready! Print status messages.
     */
    printf("[INFO] Server started, waiting for client connections...\n");
    printf("[INFO] Server listening on port %d\n", PORT);
    
    /*
     * STEP 6: MAIN SERVER LOOP - ACCEPT AND HANDLE CLIENTS
     * 
     * This infinite loop is the heart of the server:
     * 1. Wait for a client to connect (blocking call)
     * 2. Handle that client's session
     * 3. When client disconnects, go back to step 1
     * 
     * This is a simple sequential server - it handles one client at a time.
     * A production server would typically use threads or fork() to handle
     * multiple clients simultaneously.
     */
    while (1) {
        /*
         * accept() waits for a client to connect.
         * 
         * This is a BLOCKING call - the program stops here until a client
         * connects. When a client connects:
         * - Returns a new socket (client_socket) for talking to that client
         * - Fills in client_addr with the client's address information
         * 
         * Parameters:
         * - server_socket: The listening socket
         * - (struct sockaddr *)&client_addr: Where to store client's address
         * - &client_addr_len: Size of the address structure
         * 
         * Returns: New socket for the client connection, or -1 on error
         * 
         * Think of this like answering the phone when it rings.
         */
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, 
                             &client_addr_len);
        
        /*
         * Check if accept() failed.
         * If it did, log the error but continue - don't shut down the
         * entire server just because one accept() call failed.
         */
        if (client_socket == -1) {
            perror("Accept failed");
            continue;  // Go back to top of loop, wait for next client
        }
        
        /*
         * HANDLE THE CLIENT
         * 
         * handle_client() takes over here. It will:
         * - Receive commands from the client
         * - Execute them
         * - Send results back
         * - Continue until client disconnects
         * 
         * When handle_client() returns, the client has disconnected,
         * and we loop back to accept() to wait for the next client.
         */
        handle_client(client_socket);
        
        /*
         * Loop repeats - ready for the next client connection
         */
    }
    
    /*
     * This line is never reached (infinite loop above), but if it were,
     * we'd close the server socket to clean up.
     */
    close(server_socket);
    return 0;
}