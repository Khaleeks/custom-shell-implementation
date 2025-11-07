/*
 * server_multithreaded.c - Multithreaded Network Shell Server Implementation
 * 
 * This file implements a multithreaded TCP server that can handle multiple
 * clients simultaneously. Each client connection is handled by a separate
 * thread, allowing parallel command execution.
 * 
 * 
 * Key Features:
 * - Concurrent client handling using POSIX threads (pthreads)
 * - Thread-safe client tracking and numbering
 * - Detailed logging with client identification (IP, port, client number)
 * - Automatic thread cleanup using detached threads
 * - Graceful handling of client connections and disconnections
 */

#include "shell.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h> 

/*
 * CONFIGURATION CONSTANTS
 */
#define PORT 8080                      // TCP port the server listens on
#define BUFFER_SIZE 4096               // Size of buffers for command output
#define EMPTY_RESPONSE_MARKER "\x01"  // Special marker for commands with no output
#define MAX_CLIENTS 100                // Maximum number of concurrent clients

/*
 * CLIENT INFORMATION STRUCTURE
 * 
 * This structure holds all information needed to handle a client connection
 * in a separate thread. It's passed to each thread when created.
 * 
 * WHY WE NEED THIS:
 * When we create a thread with pthread_create(), we can only pass ONE pointer
 * as an argument. To pass multiple pieces of information (socket, IP, port, etc.),
 * we bundle them together in this structure.
 */
typedef struct {
    int socket;                       // Client's socket file descriptor
    char ip[INET_ADDRSTRLEN];        // Client's IP address (e.g., "192.168.1.100")
    int port;                         // Client's port number
    int client_number;                // Sequential client number for identification
} ClientInfo;

/*
 * GLOBAL VARIABLES FOR THREAD SAFETY
 * 
 * These variables are shared across ALL threads and need special protection
 * to prevent race conditions.
 * 
 * WHAT IS A RACE CONDITION?
 * Imagine two threads trying to increment global_client_counter at the same time:
 *   Thread 1 reads: counter = 5
 *   Thread 2 reads: counter = 5  (before Thread 1 writes back!)
 *   Thread 1 writes: counter = 6
 *   Thread 2 writes: counter = 6  (WRONG! Should be 7)
 * 
 * Mutexes (mutual exclusion locks) prevent this by ensuring only one thread
 * can access the protected resource at a time.
 */
static int global_client_counter = 0;     // Counter for assigning client numbers
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;  // Protects counter
static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;   // Protects console output

/**
 * ============================================================================
 * FUNCTION: capture_command_output
 * ============================================================================
 * 
 * Execute a command pipeline and capture its stdout and stderr output.
 * This is identical to Phase 2, but included here for completeness.
 * 
 * HOW IT WORKS:
 * 1. Create two pipes: one for stdout, one for stderr
 * 2. Fork a child process
 * 3. Child redirects its stdout/stderr to pipes, then executes command
 * 4. Parent reads from pipes to capture all output
 * 5. Parent waits for child to complete
 * 
 * @param pipeline: The parsed command(s) to execute
 * @param output_buf: Buffer to store captured stdout
 * @param error_buf: Buffer to store captured stderr
 * @param output_size: Size of output buffer
 * @param error_size: Size of error buffer
 * @return: 0 on success, -1 on failure
 */
int capture_command_output(Pipeline *pipeline, char *output_buf, char *error_buf, 
                          size_t output_size, size_t error_size) {
    int stdout_pipe[2];  // Pipe for capturing stdout: [0]=read end, [1]=write end
    int stderr_pipe[2];  // Pipe for capturing stderr: [0]=read end, [1]=write end
    
    // Initialize buffers to empty strings
    output_buf[0] = '\0';
    error_buf[0] = '\0';
    
    /*
     * Create the pipes. If either fails, we can't capture output.
     */
    if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
        perror("pipe failed");
        return -1;
    }
    
    /*
     * Fork a child process to execute the command.
     * The parent will capture the child's output through the pipes.
     */
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork failed");
        // Clean up pipes on failure
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return -1;
    }
    
    if (pid == 0) {
        /*
         * CHILD PROCESS PATH
         * 
         * The child's job is to:
         * 1. Redirect stdout and stderr to the pipes
         * 2. Execute the command
         * 3. Exit (never returns to this code)
         */
        
        // Close read ends - child only writes to pipes
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        // Redirect stdout to stdout_pipe write end
        dup2(stdout_pipe[1], STDOUT_FILENO);
        // Redirect stderr to stderr_pipe write end
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        // Close original pipe file descriptors (we have duplicates now)
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        // Execute the command - if successful, this never returns
        execute_pipeline(pipeline);
        exit(EXIT_SUCCESS);
    } else {
        /*
         * PARENT PROCESS PATH
         * 
         * The parent's job is to:
         * 1. Read all output from the pipes
         * 2. Store it in the provided buffers
         * 3. Wait for child to complete
         */
        
        // Close write ends - parent only reads from pipes
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        /*
         * Read stdout from pipe in chunks until no more data.
         * We read in a loop because the pipe might not contain all
         * data in a single read() call.
         */
        ssize_t bytes_read = 0;
        ssize_t total_read = 0;
        
        while ((bytes_read = read(stdout_pipe[0], output_buf + total_read, 
                                 output_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            // Stop if buffer is full (leave room for null terminator)
            if (total_read >= (ssize_t)output_size - 1) break;
        }
        output_buf[total_read] = '\0';  // Null-terminate the string
        
        /*
         * Read stderr from pipe (same process as stdout)
         */
        total_read = 0;
        while ((bytes_read = read(stderr_pipe[0], error_buf + total_read, 
                                 error_size - total_read - 1)) > 0) {
            total_read += bytes_read;
            if (total_read >= (ssize_t)error_size - 1) break;
        }
        error_buf[total_read] = '\0';
        
        // Close read ends - we're done reading
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        /*
         * Wait for child process to complete.
         * This prevents zombie processes and ensures command finishes
         * before we return.
         */
        int status;
        waitpid(pid, &status, 0);
    }
    
    return 0;
}

/**
 * ============================================================================
 * FUNCTION: thread_safe_printf
 * ============================================================================
 * 
 * Thread-safe wrapper around printf that ensures output from different
 * threads doesn't get interleaved.
 * 
 * WHY THIS IS NEEDED:
 * printf() is NOT thread-safe for multi-line output. Without synchronization,
 * when multiple threads call printf() simultaneously, their output can get
 * mixed together:
 * 
 * Thread 1: "[INFO] Client #1 connected..."
 * Thread 2: "[INFO] Client #2 connected..."
 * 
 * Without mutex, you might see:
 * "[INFO] Client #1 [INFO] Client #2 connected...connected..."
 * 
 * With mutex, output is properly serialized:
 * "[INFO] Client #1 connected..."
 * "[INFO] Client #2 connected..."
 * 
 * HOW IT WORKS:
 * 1. Lock the mutex (wait if another thread holds it)
 * 2. Print the message
 * 3. Flush output to ensure it appears immediately
 * 4. Unlock the mutex (allow other threads to print)
 * 
 * @param format: Printf-style format string
 * @param ...: Variable arguments for the format string
 */
void thread_safe_printf(const char *format, ...) {
    // Lock the output mutex - only one thread can print at a time
    pthread_mutex_lock(&output_mutex);
    
    // Variable argument list handling (like printf does internally)
    va_list args;
    va_start(args, format);       // Initialize argument list
    vprintf(format, args);         // Print with variable arguments
    va_end(args);                  // Clean up argument list
    fflush(stdout);                // Force immediate output display
    
    // Unlock the mutex - allow other threads to print
    pthread_mutex_unlock(&output_mutex);
}

/**
 * ============================================================================
 * FUNCTION: handle_client_thread
 * ============================================================================
 * 
 * Thread function that handles all communication with a single client.
 * This function runs in a separate thread for each connected client,
 * allowing multiple clients to be served simultaneously.
 * 
 * THREAD LIFECYCLE:
 * 1. Thread is created when client connects (via pthread_create)
 * 2. Extracts client info from parameter
 * 3. Enters command processing loop
 * 4. Exits when client disconnects or sends "exit"
 * 5. Thread automatically cleans up (detached mode)
 * 
 * IMPORTANT: This function runs concurrently with other client threads
 * and the main server thread. Multiple copies of this function can be
 * executing simultaneously, each handling a different client.
 * 
 * @param arg: Pointer to ClientInfo structure (cast from void*)
 * @return: NULL (required by pthread function signature)
 */
void* handle_client_thread(void* arg) {
    /*
     * EXTRACT CLIENT INFORMATION
     * 
     * The parameter is passed as void* to match pthread requirements.
     * We cast it back to ClientInfo* to access the client's information.
     * 
     * We copy all values to local variables so we can free the ClientInfo
     * structure immediately. This prevents memory leaks.
     */
    ClientInfo* client = (ClientInfo*)arg;
    int client_socket = client->socket;
    char client_ip[INET_ADDRSTRLEN];
    strcpy(client_ip, client->ip);
    int client_port = client->port;
    int client_num = client->client_number;
    
    /*
     * Free the ClientInfo structure now that we've copied its contents.
     * This structure was dynamically allocated in main() and passed to us.
     * We're responsible for freeing it to prevent memory leaks.
     */
    free(client);
    
    /*
     * DECLARE BUFFERS FOR COMMUNICATION
     * These buffers are LOCAL to this thread - each client thread has its own.
     */
    char command_buffer[MAX_INPUT_SIZE];  // Stores command received from client
    char output_buffer[BUFFER_SIZE];      // Stores command's stdout output
    char error_buffer[BUFFER_SIZE];       // Stores command's stderr output
    
    /*
     * MAIN CLIENT COMMUNICATION LOOP
     * 
     * This loop continues until:
     * - Client disconnects (recv returns 0 or -1)
     * - Client sends "exit" command
     * 
     * Each iteration:
     * 1. Receives a command from the client
     * 2. Parses and validates the command
     * 3. Executes the command and captures output
     * 4. Sends the output back to the client
     */
    while (1) {
        // Clear buffers for new command
        memset(command_buffer, 0, MAX_INPUT_SIZE);
        memset(output_buffer, 0, BUFFER_SIZE);
        memset(error_buffer, 0, BUFFER_SIZE);
        
        /*
         * RECEIVE COMMAND FROM CLIENT
         * 
         * recv() blocks until data arrives or connection closes.
         * Returns:
         *   > 0: Number of bytes received
         *   = 0: Client closed connection gracefully
         *   < 0: Error occurred
         */
        ssize_t bytes_received = recv(client_socket, command_buffer, 
                                     MAX_INPUT_SIZE - 1, 0);
        
        if (bytes_received <= 0) {
            /*
             * Client disconnected or error occurred.
             * Either way, we exit the loop and clean up.
             */
            thread_safe_printf("[INFO] Client #%d disconnected.\n", client_num);
            break;
        }
        
        // Null-terminate the received string
        command_buffer[bytes_received] = '\0';
        
        /*
         * Remove trailing newline character if present.
         * Many clients (like telnet) send commands with '\n' at the end.
         */
        size_t len = strlen(command_buffer);
        if (len > 0 && command_buffer[len - 1] == '\n') {
            command_buffer[len - 1] = '\0';
        }
        
        /*
         * LOG RECEIVED COMMAND
         * Format: [RECEIVED] [Client #X - IP:PORT] Received command: "cmd"
         * 
         * This helps with debugging and monitoring server activity.
         */
        thread_safe_printf("[RECEIVED] [Client #%d - %s:%d] Received command: \"%s\"\n",
                          client_num, client_ip, client_port, command_buffer);
        
        /*
         * CHECK FOR EXIT COMMAND
         * 
         * If client sends "exit", they want to disconnect.
         * We send a goodbye message and exit the loop.
         */
        if (strcmp(command_buffer, "exit") == 0) {
            thread_safe_printf("[INFO] [Client #%d - %s:%d] Client requested disconnect. "
                             "Closing connection.\n",
                             client_num, client_ip, client_port);
            
            const char *exit_msg = "Disconnected from server.\n";
            send(client_socket, exit_msg, strlen(exit_msg), 0);
            break;  // Exit loop and clean up
        }
        
        /*
         * SKIP EMPTY COMMANDS
         * 
         * If user just pressed Enter with no command, ignore it
         * and wait for the next command.
         */
        if (strlen(command_buffer) == 0) {
            continue;
        }
        
        /*
         * LOG COMMAND EXECUTION START
         * 
         * Indicates we're about to process the command.
         */
        thread_safe_printf("[EXECUTING] [Client #%d - %s:%d] Executing command: \"%s\"\n",
                          client_num, client_ip, client_port, command_buffer);
        
        /*
         * PARSE THE COMMAND
         * 
         * Convert the raw command string into a structured Pipeline object.
         * This breaks down the command into individual components (command name,
         * arguments, redirections, pipes, etc.).
         * 
         * Returns NULL if parsing fails (e.g., syntax error).
         */
        Pipeline *pipeline = parse_input(command_buffer);
        
        if (pipeline == NULL) {
            // Parsing failed - send error to client
            snprintf(output_buffer, BUFFER_SIZE, "Error: Failed to parse command.\n");
            thread_safe_printf("[ERROR] [Client #%d - %s:%d] Failed to parse command.\n",
                             client_num, client_ip, client_port);
            thread_safe_printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n"
                             "\"Error: Failed to parse command.\"\n",
                             client_num, client_ip, client_port);
            send(client_socket, output_buffer, strlen(output_buffer), 0);
            continue;  // Skip to next command
        }
        
        /*
         * VALIDATE THE PIPELINE
         * 
         * Check for semantic errors like:
         * - Empty commands
         * - Missing redirection files
         * - Non-existent input files
         * 
         * Returns 1 if valid, 0 if invalid.
         */
        if (!validate_pipeline(pipeline)) {
            // Validation failed - send error to client
            snprintf(output_buffer, BUFFER_SIZE, "Error: Invalid command syntax.\n");
            thread_safe_printf("[ERROR] [Client #%d - %s:%d] Invalid command syntax.\n",
                             client_num, client_ip, client_port);
            thread_safe_printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n"
                             "\"Error: Invalid command syntax.\"\n",
                             client_num, client_ip, client_port);
            send(client_socket, output_buffer, strlen(output_buffer), 0);
            free_pipeline(pipeline);  // Clean up pipeline structure
            continue;  // Skip to next command
        }
        
        /*
         * EXECUTE THE COMMAND AND CAPTURE OUTPUT
         * 
         * This runs the command in a child process and captures both
         * stdout and stderr into our buffers.
         * 
         * Returns 0 on success, -1 on failure.
         */
        int exec_result = capture_command_output(pipeline, output_buffer, 
                                                error_buffer, BUFFER_SIZE, BUFFER_SIZE);
        
        // Free pipeline structure now that we're done with it
        free_pipeline(pipeline);
        
        if (exec_result == -1) {
            // Execution failed - send error to client
            snprintf(output_buffer, BUFFER_SIZE, "Error: Failed to execute command.\n");
            thread_safe_printf("[ERROR] [Client #%d - %s:%d] Failed to execute command.\n",
                             client_num, client_ip, client_port);
            thread_safe_printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n"
                             "\"Error: Failed to execute command.\"\n",
                             client_num, client_ip, client_port);
            send(client_socket, output_buffer, strlen(output_buffer), 0);
            continue;  // Skip to next command
        }
        
        /*
         * PREPARE AND SEND RESPONSE
         * 
         * We have three cases to handle:
         * 1. Command produced error output (send error_buffer)
         * 2. Command produced normal output (send output_buffer)
         * 3. Command produced no output (send special marker)
         * 
         * We prioritize error output over normal output if both exist.
         */
        char response[BUFFER_SIZE * 2];  // Large buffer for response
        memset(response, 0, sizeof(response));
        
        if (strlen(error_buffer) > 0) {
            /*
             * Command produced error output (stderr).
             * This could be an actual error or just diagnostic messages.
             */
            thread_safe_printf("[ERROR] [Client #%d - %s:%d] Command produced error output.\n",
                             client_num, client_ip, client_port);
            thread_safe_printf("[OUTPUT] [Client #%d - %s:%d] Sending error message to client:\n"
                             "\"%s\"\n",
                             client_num, client_ip, client_port, error_buffer);
            snprintf(response, sizeof(response), "%s", error_buffer);
        } else if (strlen(output_buffer) > 0) {
            /*
             * Command produced normal output (stdout).
             * This is the typical case for successful commands.
             */
            thread_safe_printf("[OUTPUT] [Client #%d - %s:%d] Sending output to client:\n%s",
                             client_num, client_ip, client_port, output_buffer);
            snprintf(response, sizeof(response), "%s", output_buffer);
        } else {
            /*
             * Command executed successfully but produced no output.
             * Examples: "touch file.txt", "mkdir directory"
             * 
             * We send a special marker so the client knows the command
             * completed successfully (not an error or disconnect).
             */
            thread_safe_printf("[OUTPUT] [Client #%d - %s:%d] Command executed successfully "
                             "with no output.\n",
                             client_num, client_ip, client_port);
            snprintf(response, sizeof(response), "%s", EMPTY_RESPONSE_MARKER);
        }
        
        /*
         * SEND RESPONSE TO CLIENT
         * 
         * send() transmits the response over the network socket.
         * Returns number of bytes sent, or -1 on error.
         */
        ssize_t sent = send(client_socket, response, strlen(response), 0);
        if (sent == -1) {
            // Send failed - probably client disconnected
            thread_safe_printf("[ERROR] [Client #%d - %s:%d] Send failed.\n",
                             client_num, client_ip, client_port);
            break;  // Exit loop and clean up
        }
        
        /*
         * Command processed successfully!
         * Loop back to wait for the next command from this client.
         */
    }
    
    /*
     * CLEANUP AND EXIT THREAD
     * 
     * We reach here when:
     * - Client disconnected
     * - Client sent "exit"
     * - A send() failed
     * 
     * Close the client's socket and exit the thread.
     * Since the thread is detached, all resources are automatically cleaned up.
     */
    close(client_socket);
    thread_safe_printf("[INFO] Client #%d disconnected.\n", client_num);
    
    return NULL;  // Thread exits
}

/**
 * ============================================================================
 * FUNCTION: main
 * ============================================================================
 * 
 * Main server function that sets up the TCP server and spawns threads
 * to handle each connecting client.
 * 
 * FLOW:
 * 1. Create and configure server socket
 * 2. Bind socket to address and port
 * 3. Start listening for connections
 * 4. Loop forever:
 *    a. Accept new client connection (blocks until client connects)
 *    b. Create ClientInfo structure with client details
 *    c. Spawn new thread to handle the client
 *    d. Detach thread for automatic cleanup
 *    e. Go back to step 4a (accept next client)
 * 
 * KEY DIFFERENCES FROM SEQUENTIAL SERVER (PHASE 2):
 * - Uses pthread_create() to spawn a new thread for each client
 * - Threads are detached for automatic cleanup
 * - Main thread continues accepting new connections immediately
 * - Multiple clients can be served simultaneously
 * - Each client gets independent execution in its own thread
 */
int main(void) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t thread_id;  // Will hold thread ID for each new thread
    
    /*
     * CREATE SERVER SOCKET
     * 
     * socket() creates an endpoint for communication.
     * AF_INET: IPv4 address family
     * SOCK_STREAM: TCP (reliable, connection-oriented)
     * 0: Let system choose appropriate protocol (TCP for SOCK_STREAM)
     * 
     * Returns file descriptor for the socket, or -1 on error.
     */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    /*
     * SET SOCKET OPTIONS
     * 
     * SO_REUSEADDR allows the server to immediately reuse the port
     * after a restart, without waiting for the OS timeout period.
     * 
     * Without this, you'd get "Address already in use" errors when
     * restarting the server quickly.
     */
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    /*
     * CONFIGURE SERVER ADDRESS
     * 
     * Set up the address structure that defines where the server listens:
     * - sin_family: IPv4
     * - sin_addr: INADDR_ANY (listen on all network interfaces)
     * - sin_port: PORT (8080), converted to network byte order with htons()
     * 
     * htons() converts "host to network short" - ensures proper byte order
     * regardless of system architecture (big-endian vs little-endian).
     */
    memset(&server_addr, 0, sizeof(server_addr));  // Zero out structure
    server_addr.sin_family = AF_INET;              // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;      // Any interface (0.0.0.0)
    server_addr.sin_port = htons(PORT);            // Port 8080
    
    /*
     * BIND SOCKET TO ADDRESS
     * 
     * bind() associates the socket with a specific IP address and port.
     * After this, the socket is "named" and ready to accept connections
     * on that address/port combination.
     * 
     * This is like claiming "port 8080 is mine" from the OS.
     */
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    /*
     * START LISTENING FOR CONNECTIONS
     * 
     * listen() marks the socket as passive - ready to accept incoming
     * connection requests.
     * 
     * MAX_CLIENTS is the backlog - maximum number of pending connections
     * that can queue up while we're busy accepting others.
     */
    if (listen(server_socket, MAX_CLIENTS) == -1) {
        perror("Listen failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("[INFO] Server started, waiting for client connections...\n");
    
    /*
     * MAIN SERVER LOOP - ACCEPT CLIENTS AND SPAWN THREADS
     * 
     * This loop runs forever, accepting new client connections and
     * spawning a thread to handle each one.
     * 
     * The beauty of this design: the main thread is ALWAYS available
     * to accept new clients, no matter how many clients are currently
     * connected or what they're doing.
     */
    while (1) {
        /*
         * ACCEPT NEW CLIENT CONNECTION
         * 
         * accept() blocks (waits) until a client connects.
         * When a client connects:
         * - Returns a NEW socket for communicating with that client
         * - Fills client_addr with the client's IP address and port
         * 
         * The original server_socket remains listening for more connections.
         * The new client_socket is specifically for this one client.
         */
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, 
                             &client_addr_len);
        
        if (client_socket == -1) {
            perror("Accept failed");
            continue;  // Skip this client, keep accepting others
        }
        
        /*
         * ALLOCATE AND POPULATE CLIENT INFO STRUCTURE
         * 
         * We create a ClientInfo structure on the heap (using malloc)
         * because it needs to survive after we pass it to the thread.
         * 
         * The thread will be responsible for freeing this memory.
         * 
         * WHY HEAP, NOT STACK?
         * If we used a stack variable, it would be destroyed when this
         * loop iteration ends, but the thread might not have read it yet!
         */
        ClientInfo* client_info = malloc(sizeof(ClientInfo));
        if (client_info == NULL) {
            perror("malloc failed");
            close(client_socket);
            continue;  // Skip this client
        }
        
        // Store client's socket
        client_info->socket = client_socket;
        
        // Convert client's IP from binary to string format
        inet_ntop(AF_INET, &client_addr.sin_addr, client_info->ip, INET_ADDRSTRLEN);
        
        // Convert client's port from network byte order to host byte order
        client_info->port = ntohs(client_addr.sin_port);
        
        /*
         * ASSIGN CLIENT NUMBER (THREAD-SAFE)
         * 
         * We need to assign a unique number to each client for logging.
         * Since multiple threads could be accepting clients simultaneously
         * in a more advanced design, we use a mutex to protect the counter.
         * 
         * CRITICAL SECTION (protected by mutex):
         * 1. Lock mutex
         * 2. Increment counter
         * 3. Assign number to this client
         * 4. Unlock mutex
         * 
         * This ensures no two clients get the same number.
         */
        pthread_mutex_lock(&counter_mutex);
        global_client_counter++;
        client_info->client_number = global_client_counter;
        pthread_mutex_unlock(&counter_mutex);
        
        /*
         * LOG CLIENT CONNECTION
         * 
         * Announce that a new client has connected and which thread
         * will handle them.
         */
        thread_safe_printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n",
                          client_info->client_number, client_info->ip, 
                          client_info->port, client_info->client_number);
        
        /*
         * CREATE NEW THREAD TO HANDLE CLIENT
         * 
         * pthread_create() spawns a new thread that starts executing
         * the handle_client_thread() function.
         * 
         * Parameters:
         * - &thread_id: Where to store the new thread's ID
         * - NULL: Default thread attributes (standard stack size, etc.)
         * - handle_client_thread: Function the new thread will execute
         * - client_info: Argument passed to the thread function (void*)
         * 
         * The new thread starts running immediately and independently.
         * This function returns immediately, allowing us to accept
         * the next client without waiting for this one to finish.
         * 
         * THREAD INDEPENDENCE:
         * Once created, the thread runs independently. Even if this
         * main thread is busy accepting another client, the spawned
         * thread continues processing its client's commands.
         */
        if (pthread_create(&thread_id, NULL, handle_client_thread, client_info) != 0) {
            perror("Thread creation failed");
            free(client_info);  // Clean up since thread wasn't created
            close(client_socket);
            continue;  // Skip this client, keep accepting others
        }
        
        /*
         * DETACH THE THREAD
         * 
         * pthread_detach() tells the system: "I don't care about this
         * thread's return value, so automatically clean up all its
         * resources when it exits."
         * 
         * WITHOUT DETACHING:
         * - Thread resources aren't freed until we call pthread_join()
         * - We'd need to track all thread IDs and join them later
         * - This would be complex and unnecessary for a server
         * 
         * WITH DETACHING:
         * - Thread resources are automatically freed when thread exits
         * - We don't need to track thread IDs
         * - Perfect for "fire and forget" worker threads
         * 
         * ANALOGY:
         * Think of a detached thread like hiring a contractor for a job.
         * You don't need to supervise them or wait for them to finish -
         * they do their work independently and clean up when done.
         */
        pthread_detach(thread_id);
        
        /*
         * Main thread immediately loops back to accept() to handle next client.
         * The spawned thread handles the current client independently.
         * 
         * This is what makes the server "multithreaded" - we can accept
         * new clients while other threads are busy serving existing clients.
         * 
         * CONCURRENCY IN ACTION:
         * At this point in the code, multiple things are happening:
         * - This main thread goes back to accept() to wait for next client
         * - Thread 1 might be executing a "ls" command for Client #1
         * - Thread 2 might be waiting for Client #2 to type a command
         * - Thread 3 might be sending output back to Client #3
         * 
         * All happening simultaneously!
         */
    }
    
    /*
     * CLEANUP
     * 
     * We never actually reach this code because the while(1) loop
     * runs forever. But if we did (e.g., if we added a shutdown signal),
     * we'd close the server socket here.
     */
    close(server_socket);
    return 0;
}