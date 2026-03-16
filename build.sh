#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# build.sh  —  Compile chess.cpp → chess.js + chess.wasm via Emscripten
#
# Prerequisites
# ─────────────
#   1. Install Emscripten SDK:
#        git clone https://github.com/emscripten-core/emsdk.git
#        cd emsdk
#        ./emsdk install latest
#        ./emsdk activate latest
#        source ./emsdk_env.sh     # or emsdk_env.bat on Windows
#
#   2. Verify installation:
#        em++ --version
#
# Usage
# ─────
#   chmod +x build.sh
#   ./build.sh
#
#   This produces:  chess.js   (Emscripten glue + module loader)
#                   chess.wasm (compiled C++ binary)
#
# Running
# ───────
#   You MUST serve the files over HTTP (not file://) because browsers
#   block WASM loading from the local filesystem.
#
#   Quick local server options:
#     Python 3:   python3 -m http.server 8080
#     Node.js:    npx serve .
#     VS Code:    "Live Server" extension → right-click index.html → Open
#
#   Then open:    http://localhost:8080
# ─────────────────────────────────────────────────────────────────────────────

set -e  # exit immediately on any error

echo "──────────────────────────────────────"
echo " Compiling C++ Chess Engine → WASM"
echo "──────────────────────────────────────"

em++ chess.cpp \
  -O3 \
  -o chess.js \
  --bind \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="ChessEngine" \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT="web" \
  --closure 0

echo ""
echo "✓ Build successful!"
echo "  Output: chess.js  ($(du -sh chess.js  | cut -f1))"
echo "  Output: chess.wasm ($(du -sh chess.wasm | cut -f1))"
echo ""
echo "To play, start a local server and open http://localhost:8080:"
echo "  python3 -m http.server 8080"
