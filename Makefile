# Makefile for MyShell Project - Modular Implementation
# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g
TARGET = myshell
SOURCES = main.c input.c parser.c executor.c memory.c utils.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = shell.h

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

# Compile source files to object files with dependency on header
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Individual object file rules for clarity (optional but good practice)
main.o: main.c $(HEADERS)
	$(CC) $(CFLAGS) -c main.c -o main.o

input.o: input.c $(HEADERS)
	$(CC) $(CFLAGS) -c input.c -o input.o

parser.o: parser.c $(HEADERS)
	$(CC) $(CFLAGS) -c parser.c -o parser.o

executor.o: executor.c $(HEADERS)
	$(CC) $(CFLAGS) -c executor.c -o executor.o

memory.o: memory.c $(HEADERS)
	$(CC) $(CFLAGS) -c memory.c -o memory.o

utils.o: utils.c $(HEADERS)
	$(CC) $(CFLAGS) -c utils.c -o utils.o

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Install (optional - copies to /usr/local/bin)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Uninstall (removes from /usr/local/bin)
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Run the shell
run: $(TARGET)
	./$(TARGET)

# Debug build with additional debugging symbols
debug: CFLAGS += -DDEBUG -g3
debug: clean $(TARGET)

# Create test files for testing
test-files:
	echo "Hello World" > input.txt
	echo "Line 1" > test_input.txt
	echo "Line 2" >> test_input.txt
	echo "Line 3" >> test_input.txt
	echo "apple" > fruits.txt
	echo "banana" >> fruits.txt
	echo "cherry" >> fruits.txt

# Clean test files
clean-test:
	rm -f input.txt test_input.txt output.txt error.log fruits.txt
	rm -f sorted_fruits.txt word_count.txt current_time.txt test_output.txt error_output.txt

# Clean everything (build artifacts and test files)
clean-all: clean clean-test

# Check for memory leaks with valgrind (if available)
memcheck: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# Show project structure
structure:
	@echo "Project Structure:"
	@echo "├── $(TARGET) (executable)"
	@echo "├── Makefile"
	@echo "├── shell.h (header file)"
	@echo "├── main.c (entry point)"
	@echo "├── input.c (input handling)"
	@echo "├── parser.c (command parsing)"
	@echo "├── executor.c (command execution)"
	@echo "├── memory.c (memory management)"
	@echo "└── utils.c (utility functions)"

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build the myshell executable (default)"
	@echo "  clean      - Remove object files and executable"
	@echo "  run        - Build and run the shell"
	@echo "  debug      - Build with debug symbols"
	@echo "  test-files - Create test files for testing"
	@echo "  clean-test - Remove test files"
	@echo "  clean-all  - Remove all generated files"
	@echo "  memcheck   - Run with valgrind memory checking"
	@echo "  structure  - Show project file structure"
	@echo "  install    - Install to /usr/local/bin/"
	@echo "  uninstall  - Remove from /usr/local/bin/"
	@echo "  help       - Show this help message"

# Force rebuild
rebuild: clean $(TARGET)

# Check coding style (if you have a style checker)
style-check:
	@echo "Checking code style..."
	@if command -v indent >/dev/null 2>&1; then \
		indent -linux $(SOURCES) $(HEADERS); \
		echo "Code formatted with indent"; \
	else \
		echo "indent not available, skipping style check"; \
	fi

# Phony targets
.PHONY: all clean run debug test-files clean-test clean-all memcheck structure help rebuild install uninstall style-check