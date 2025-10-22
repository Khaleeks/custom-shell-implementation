# Makefile for Remote Shell (Phase 2)
# Compiles both server and client programs with Phase 1 shell functionality

CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS = 

# Object files for the shell core (Phase 1 functionality)
SHELL_OBJS = executor.o parser.o memory.o utils.o

# Server executable and its specific objects
SERVER_TARGET = server
SERVER_OBJS = server.o $(SHELL_OBJS)

# Client executable (standalone, no shell dependencies)
CLIENT_TARGET = client
CLIENT_OBJS = client.o

# Default target: build both server and client
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Build server executable
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $(SERVER_TARGET) $(SERVER_OBJS) $(LDFLAGS)
	@echo "Server built successfully!"

# Build client executable
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $(CLIENT_TARGET) $(CLIENT_OBJS) $(LDFLAGS)
	@echo "Client built successfully!"

# Compile server.c
server.o: server.c shell.h
	$(CC) $(CFLAGS) -c server.c

# Compile client.c
client.o: client.c
	$(CC) $(CFLAGS) -c client.c

# Compile executor.c (Phase 1)
executor.o: executor.c shell.h
	$(CC) $(CFLAGS) -c executor.c

# Compile parser.c (Phase 1)
parser.o: parser.c shell.h
	$(CC) $(CFLAGS) -c parser.c

# Compile memory.c (Phase 1)
memory.o: memory.c shell.h
	$(CC) $(CFLAGS) -c memory.c

# Compile utils.c (Phase 1)
utils.o: utils.c shell.h
	$(CC) $(CFLAGS) -c utils.c

# Clean all build artifacts
clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_TARGET) $(CLIENT_TARGET)
	@echo "Cleaned build artifacts."

# Rebuild everything from scratch
rebuild: clean all

# Help target to show available commands
help:
	@echo "Available targets:"
	@echo "  all      - Build both server and client (default)"
	@echo "  server   - Build only the server"
	@echo "  client   - Build only the client"
	@echo "  clean    - Remove all build artifacts"
	@echo "  rebuild  - Clean and rebuild everything"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make           # Build both programs"
	@echo "  make clean     # Clean build files"
	@echo "  make rebuild   # Clean and rebuild"

.PHONY: all clean rebuild help