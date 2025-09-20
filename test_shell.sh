#!/bin/bash

# Test script for MyShell implementation
# This script creates test files and demonstrates various shell features

echo "=== MyShell Test Script ==="
echo "Creating test files..."

# Create test input files
echo "Hello World" > input.txt
echo "Line 1" > test_input.txt
echo "Line 2" >> test_input.txt
echo "Line 3" >> test_input.txt
echo "apple" > fruits.txt
echo "banana" >> fruits.txt
echo "cherry" >> fruits.txt

# Create a simple executable for testing
cat > hello.c << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello from custom program!\n");
    return 0;
}
EOF

gcc -o hello hello.c

echo "Test files created successfully!"
echo ""

echo "=== Manual Testing Commands ==="
echo "Copy and paste these commands into your shell to test different features:"
echo ""

echo "1. Basic Commands:"
echo "   ls"
echo "   ps"
echo "   pwd"
echo "   date"
echo ""

echo "2. Commands with Arguments:"
echo "   ls -l"
echo "   ps aux"
echo "   wc -l fruits.txt"
echo ""

echo "3. Input Redirection:"
echo "   cat < input.txt"
echo "   wc < fruits.txt"
echo "   sort < fruits.txt"
echo ""

echo "4. Output Redirection:"
echo "   ls > output.txt"
echo "   date > current_time.txt"
echo "   echo 'Test output' > test_output.txt"
echo ""

echo "5. Error Redirection:"
echo "   invalid_command 2> error.log"
echo "   ls /nonexistent 2> error_output.txt"
echo ""

echo "6. Simple Pipes:"
echo "   ls | wc -l"
echo "   cat fruits.txt | sort"
echo "   ps aux | grep root"
echo "   ls -l | head -5"
echo ""

echo "7. Multiple Pipes:"
echo "   cat fruits.txt | sort | wc -l"
echo "   ls -l | grep txt | wc -l"
echo "   ps aux | grep root | head -3"
echo ""

echo "8. Combined Redirections and Pipes:"
echo "   cat < fruits.txt | sort > sorted_fruits.txt"
echo "   ls | grep txt > text_files.txt"
echo "   cat fruits.txt | sort | head -2 > top_fruits.txt"
echo ""

echo "9. Complex Combinations:"
echo "   cat < input.txt | wc > word_count.txt"
echo "   ls -l | grep txt | wc -l > txt_count.txt"
echo "   cat fruits.txt | sort | tail -1 > last_fruit.txt"
echo ""

echo "10. Executable Programs:"
echo "    ./hello"
echo ""

echo "11. Error Testing:"
echo "    nonexistent_command"
echo "    cat < nonexistent_file.txt"
echo "    ls |"
echo "    | ls"
echo "    ls > "
echo "    ls < "
echo "    ls 2> "
echo ""

echo "12. Exit Shell:"
echo "    exit"
echo ""

echo "=== Automated Test Results Verification ==="
echo "After running the shell tests, you can verify results with:"
echo ""

# Function to check if file exists and show content
check_file() {
    if [ -f "$1" ]; then
        echo "✓ $1 exists:"
        head -3 "$1" 2>/dev/null
        echo ""
    else
        echo "✗ $1 not found"
        echo ""
    fi
}

echo "Run these commands after testing to verify outputs:"
echo "check_file() { if [ -f \"\$1\" ]; then echo \"✓ \$1:\"; head -3 \"\$1\"; else echo \"✗ \$1 not found\"; fi; echo; }"
echo ""
echo "check_file output.txt"
echo "check_file sorted_fruits.txt"
echo "check_file word_count.txt"
echo "check_file error.log"
echo "check_file current_time.txt"
echo ""

echo "=== Cleanup Commands ==="
echo "After testing, clean up with:"
echo "rm -f *.txt *.log hello hello.c"
echo ""

echo "Now compile and run your shell:"
echo "make clean && make"
echo "./myshell"