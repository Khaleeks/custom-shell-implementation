# Makefile for Phase 3 - Multithreaded Shell Server
# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS = -pthread

# Target executables
SERVER = server
CLIENT = client

# Source files
SERVER_SOURCES = server.c parser.c executor.c memory.c utils.c
CLIENT_SOURCES = client.c

# Object files
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
CLIENT_OBJECTS = $(CLIENT_SOURCES:.c=.o)

# Header files
HEADERS = shell.h

# Default target - build both server and client
all: $(SERVER) $(CLIENT)

# Build server executable
$(SERVER): $(SERVER_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Server compiled successfully!"

# Build client executable
$(CLIENT): $(CLIENT_OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Client compiled successfully!"

# Compile source files to object files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(SERVER) $(CLIENT) *.o
	@echo "Cleaned build artifacts."

# Clean and rebuild
rebuild: clean all

# Run server
run-server: $(SERVER)
	./$(SERVER)

# Run client (use: make run-client IP=<ip_address>)
run-client: $(CLIENT)
	./$(CLIENT) $(IP)

# Help target
help:
	@echo "Makefile for Phase 3 - Multithreaded Shell Server"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build both server and client (default)"
	@echo "  server       - Build only the server"
	@echo "  client       - Build only the client"
	@echo "  clean        - Remove all build artifacts"
	@echo "  rebuild      - Clean and rebuild everything"
	@echo "  run-server   - Build and run the server"
	@echo "  run-client   - Build and run the client"
	@echo "                 Usage: make run-client IP=127.0.0.1"
	@echo "  help         - Display this help message"

.PHONY: all clean rebuild run-server run-client help