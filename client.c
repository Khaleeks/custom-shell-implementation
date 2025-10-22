#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_INPUT_SIZE 1024

/**
 * Display the shell prompt
 * 
 * Prints a "$" character followed by a space to indicate the shell is ready
 * to accept commands. fflush() is necessary because stdout is line-buffered
 * by default, and the prompt lacks a newline character.
 */
void display_prompt(void) {
    printf("$ ");
    fflush(stdout);
}

/**
 * Read input from the user
 * 
 * Allocates memory for user input and reads a complete line from stdin.
 * The trailing newline character added by fgets() is removed for cleaner
 * command processing.
 * 
 * @return: Dynamically allocated string containing user input, or NULL on EOF/error
 */
char *read_input(void) {
    char *input = malloc(MAX_INPUT_SIZE);
    if (!input) {
        perror("malloc failed");
        exit(EXIT_FAILURE);
    }
    
    // Read a line from standard input
    if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
        free(input);
        return NULL;
    }
    
    // Remove trailing newline character
    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
        input[len - 1] = '\0';
    }
    
    return input;
}

/**
 * Main client function
 * 
 * This program implements a network client that connects to a remote shell server.
 * It provides an interactive interface where users can type commands locally, which
 * are sent to the server for execution. The server's responses are then displayed
 * to the user.
 * 
 * The client operates in a request-response pattern:
 * 1. Accept user input
 * 2. Send command to server
 * 3. Receive and display server response
 * 4. Repeat until user exits
 * 
 * @param argc: Number of command-line arguments
 * @param argv: Array of command-line arguments (argv[1] can specify server IP)
 * @return: 0 on successful termination, EXIT_FAILURE on critical errors
 */
int main(int argc, char *argv[]) {
    int client_socket;
    struct sockaddr_in server_addr;
    char *input;
    char response_buffer[BUFFER_SIZE];
    const char *server_ip = "127.0.0.1";  // Default to localhost
    
    /*
     * COMMAND-LINE ARGUMENT PROCESSING
     * 
     * Allow the user to specify a custom server IP address as the first
     * command-line argument. If not provided, default to localhost (127.0.0.1).
     */
    if (argc > 1) {
        server_ip = argv[1];
    }
    
    /*
     * SOCKET CREATION
     * 
     * Create a TCP socket for communication with the server.
     * AF_INET specifies IPv4 addressing, and SOCK_STREAM indicates TCP protocol.
     */
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    /*
     * SERVER ADDRESS CONFIGURATION
     * 
     * Prepare the server address structure with:
     * - Address family (IPv4)
     * - Port number (converted to network byte order with htons)
     * - IP address of the server
     */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    
    /*
     * IP ADDRESS CONVERSION
     * 
     * Convert the IP address from text format (e.g., "127.0.0.1") to binary
     * form suitable for network operations. inet_pton() handles this conversion.
     */
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address / Address not supported");
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    /*
     * SERVER CONNECTION
     * 
     * Establish a connection to the remote server. This is a blocking operation
     * that will fail if the server is not running or is unreachable.
     */
    if (connect(client_socket, (struct sockaddr *)&server_addr, 
                sizeof(server_addr)) == -1) {
        perror("Connection failed");
        printf("Make sure the server is running on %s:%d\n", server_ip, PORT);
        close(client_socket);
        exit(EXIT_FAILURE);
    }
    
    printf("Connected to remote shell server.\n");
    printf("Type 'exit' to quit\n\n");
    
    /*
     * MAIN COMMAND LOOP
     * 
     * This loop implements the client's core functionality: reading commands
     * from the user, sending them to the server, and displaying responses.
     * The loop continues until the user types "exit" or presses Ctrl+D.
     */
    while (1) {
        // Display prompt and read user input
        display_prompt();
        input = read_input();
        
        /*
         * EOF HANDLING
         * 
         * When read_input() returns NULL (indicating EOF from Ctrl+D),
         * send an exit command to the server and terminate gracefully.
         */
        if (input == NULL) {
            printf("\n");
            
            // Notify server that client is exiting
            const char *exit_cmd = "exit";
            send(client_socket, exit_cmd, strlen(exit_cmd), 0);
            break;
        }
        
        /*
         * EMPTY INPUT HANDLING
         * 
         * If the user presses Enter without typing anything, skip the
         * send/receive cycle and display the prompt again.
         */
        if (strlen(input) == 0) {
            free(input);
            continue;
        }
        
        /*
         * SEND COMMAND TO SERVER
         * 
         * Transmit the user's command to the server. The send() function
         * returns the number of bytes sent, or -1 on error.
         */
        ssize_t sent = send(client_socket, input, strlen(input), 0);
        if (sent == -1) {
            perror("Send failed");
            free(input);
            break;
        }
        
        /*
         * EXIT COMMAND HANDLING
         * 
         * If the user types "exit", wait for the server's confirmation
         * message, display it, and then terminate the client.
         */
        if (strcmp(input, "exit") == 0) {
            free(input);
            
            // Receive exit confirmation from server
            memset(response_buffer, 0, BUFFER_SIZE);
            ssize_t bytes_received = recv(client_socket, response_buffer, 
                                         BUFFER_SIZE - 1, 0);
            if (bytes_received > 0) {
                response_buffer[bytes_received] = '\0';
                printf("%s", response_buffer);
            }
            break;
        }
        
        free(input);
        
        /*
         * RECEIVE SERVER RESPONSE
         * 
         * Wait for and receive the server's response to the command.
         * This is a blocking operation that waits until data arrives
         * or the connection is closed.
         */
        memset(response_buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_socket, response_buffer, 
                                     BUFFER_SIZE - 1, 0);
        
        // Check for receive errors
        if (bytes_received < 0) {
            perror("Receive failed");
            break;
        }
        
        // Check if server disconnected
        if (bytes_received == 0) {
            printf("Server disconnected.\n");
            break;
        }
        
        // Null-terminate the received data to create a valid C string
        response_buffer[bytes_received] = '\0';
        
        /*
         * EMPTY RESPONSE MARKER HANDLING
         * 
         * The server uses a special marker (\x01) to indicate that a command
         * executed successfully but produced no output. This distinguishes
         * between "no output" and "connection error".
         */
        if (bytes_received == 1 && response_buffer[0] == '\x01') {
            // Command executed with no output - continue to next prompt
            continue;
        }
        
        /*
         * DISPLAY SERVER RESPONSE
         * 
         * Print the server's response exactly as received. If the response
         * doesn't end with a newline, add one for better formatting.
         */
        if (strlen(response_buffer) > 0) {
            printf("%s", response_buffer);
            
            // Ensure output ends with a newline for proper formatting
            if (response_buffer[strlen(response_buffer) - 1] != '\n') {
                printf("\n");
            }
        }
    }
    
    // Clean up: close the socket before exiting
    close(client_socket);
    return 0;
}