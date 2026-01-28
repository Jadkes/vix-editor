#!/bin/bash
echo "Compiling Vix Suite..."
CFLAGS="-I/usr/include/python3.12 -std=c++17"
LDFLAGS="-lpython3.12 -lncurses"
g++ src/vix_editor.cpp -o vix $CFLAGS $LDFLAGS
g++ src/vix_agent.cpp -o vix_agent $CFLAGS $LDFLAGS
if [ $? -eq 0 ]; then echo "Success!"; else echo "Fail"; fi
