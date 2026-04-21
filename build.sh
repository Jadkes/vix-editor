#!/bin/bash
echo "🚀 Compiling Vix Suite..."

# Directories
SOURCE_DIR="/home/jadkeskes/Documents/vix-editor/src"
BIN_DIR="/home/jadkeskes/Documents/vix-editor"
INSTALL_DIR="/home/jadkeskes/.local/bin"

# Compiler Flags
CFLAGS="-I/usr/include/python3.14 -std=c++20 -Wall -Wextra -Wpedantic"
LDFLAGS="-lpython3.14 -lncurses"

# 1. Compile Vix Editor
echo ">> Building Vix Editor..."
g++ "$SOURCE_DIR/vix_editor.cpp" "$SOURCE_DIR/core/buffer.cpp" "$SOURCE_DIR/history/history.cpp" -o "$BIN_DIR/vix" $CFLAGS $LDFLAGS

# 2. Compile Vix Agent
echo ">> Building Vix Agent..."
g++ "$SOURCE_DIR/vix_agent.cpp" -o "$BIN_DIR/vix_agent" $CFLAGS $LDFLAGS

if [ $? -eq 0 ]; then
  echo "✅ Compilation Successful!"

  # 3. Create Global Symlinks
  echo ">> Installing to $INSTALL_DIR..."
  mkdir -p "$INSTALL_DIR"
  ln -sf "$BIN_DIR/vix" "$INSTALL_DIR/vix"
  ln -sf "$BIN_DIR/vix_agent" "$INSTALL_DIR/jarvis"

  echo "------------------------------------------"
  echo "🎉 Installation Complete!"
  echo "You can now run 'vix' or 'jarvis' from any folder."
  echo "------------------------------------------"
else
  echo "❌ Compilation Failed."
  exit 1
fi
