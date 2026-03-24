@echo off
echo ──────────────────────────────────────
echo  Compiling C++ Chess Engine → WASM (Windows)
echo ──────────────────────────────────────

call em++ chess.cpp ^
  -O3 ^
  -o chess.js ^
  --bind ^
  -s WASM=1 ^
  -s MODULARIZE=1 ^
  -s EXPORT_NAME="ChessEngine" ^
  -s ALLOW_MEMORY_GROWTH=1 ^
  -s ENVIRONMENT="web" ^
  --closure 0

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCCESS] Build complete!
echo   Output: chess.js
echo   Output: chess.wasm
echo.
echo To play, start a local server and open http://localhost:8080
echo Example (Node.js): npx serve .
echo.
pause
