# Chess Engine — C++ / WebAssembly try to beat my engine

A fully playable chess engine written in C++, compiled to WebAssembly
via [Emscripten](https://emscripten.org) and rendered in the browser with
a clean HTML/CSS/JS front-end.

---

## File Structure

```
chess_wasm/
├── chess.cpp      ← C++ engine  (move gen, alpha-beta search, eval, Embind API)
├── index.html     ← HTML page structure
├── style.css      ← All visual styling
├── ui.js          ← JS UI: loads WASM, renders board, handles input
├── build.sh       ← One-command Emscripten build script
└── README.md      ← This file

After building, two more files appear:
├── chess.js       ← Emscripten glue code (auto-generated)
└── chess.wasm     ← Compiled C++ binary  (auto-generated)
```

---

## How It Works

```
chess.cpp
  │
  │  em++ -O3 --bind
  ▼
chess.js + chess.wasm   ← loaded by index.html
  │
  │  ChessEngine().then(module => …)
  ▼
ui.js  ←→  C++ API via module.initGame(), module.makeEngineMoveAtDepth(5), …
  │
  ▼
HTML/CSS board rendered in the browser
```

The chess logic (move generation, minimax, evaluation) runs entirely in
the compiled C++ via WebAssembly. JavaScript only handles:

- Rendering the board (HTML/CSS)
- Translating click events into UCI move strings
- Calling the C++ API and updating the display

---

## Setup & Build of diffpal

### Step 1 — Install Emscripten

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh     # Linux/macOS
# or: emsdk_env.bat       # Windows
```

Verify:

```bash
em++ --version
```

### Step 2 — Compile

```bash
cd chess_wasm
chmod +x build.sh
./build.sh
```

This runs:

```bash
em++ chess.cpp -O3 -o chess.js \
  --bind \
  -s WASM=1 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME="ChessEngine" \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s ENVIRONMENT="web"
```

| Flag                    | Purpose                                                 |
| ----------------------- | ------------------------------------------------------- |
| `-O3`                   | Full compiler optimisations (inlining, vectorisation)   |
| `--bind`                | Enable Emscripten Embind for C++ ↔ JS function bindings |
| `MODULARIZE=1`          | Wrap the module in a factory function `ChessEngine()`   |
| `EXPORT_NAME`           | Name of the factory function                            |
| `ALLOW_MEMORY_GROWTH=1` | Heap can grow if needed                                 |
| `ENVIRONMENT=web`       | Strip unnecessary Node.js/worker code from the output   |

### Step 3 — Serve and Play

Browsers block WebAssembly from `file://` — you must use HTTP:

```bash
# Python 3
python3 -m http.server 8080

# Node.js
npx serve .
```

Open: **http://localhost:8080**

---

## Engine Details

### Board representation

Flat `int[64]` mailbox array. Piece encoding:

```
0=empty
1=wP  2=wN  3=wB  4=wR  5=wQ  6=wK
7=bP  8=bN  9=bB  10=bR 11=bQ 12=bK
```

### Move generation

- Pseudo-legal moves generated per piece type
- Legality filter: simulate each move, reject if own king is left in check
- Full support: castling, en passant, all four promotions

### Search

- **Minimax with alpha-beta pruning**
- **MVV-LVA move ordering** (captures sorted by victim value minus attacker value)
- Depth selectable via UI (3–6 plies)

### Evaluation

- **Material** (centipawns): P=100 N=320 B=330 R=500 Q=900
- **Piece-square tables** for all 6 piece types (positional bonuses)

### Estimated strength by depth

| Depth | Search time | Approx ELO |
| ----- | ----------- | ---------- |
| 3     | < 50 ms     | ~1200      |
| 4     | ~200 ms     | ~1500      |
| 5     | ~1 s        | ~1700      |
| 6     | ~5–10 s     | ~1900      |

### Why C++/WASM vs pure JavaScript?

|                     | JavaScript | C++/WASM |
| ------------------- | ---------- | -------- |
| Typical depth in 1s | 3 ply      | 5–6 ply  |
| Nodes/sec           | ~500k      | ~5–10M   |
| ELO at same depth   | baseline   | +300–400 |

C++ benefits: no JIT warm-up, no garbage collector pauses, cheaper
function calls, and the ability to use bitboards & SIMD in future.

---

## Extending the Engine

Some ideas for improving strength further:

- **Transposition table** — cache evaluated positions to avoid re-searching identical positions
- **Quiescence search** — extend search at captures to avoid the "horizon effect"
- **Bitboard representation** — represent all piece positions as 64-bit integers for ~10× faster move generation
- **Iterative deepening** — search depth 1, 2, 3… to improve move ordering at each level
- **Endgame tables** — precomputed perfect play for K+P, K+R vs K, etc.

---

## JavaScript ↔ C++ API Reference

These functions are exported from `chess.cpp` via `EMSCRIPTEN_BINDINGS`:

| Function                       | Returns | Description                               |
| ------------------------------ | ------- | ----------------------------------------- |
| `initGame()`                   | void    | Reset to starting position                |
| `getBoardJSON()`               | string  | Full board state as JSON                  |
| `getLegalMovesFromSquare(sq)`  | string  | JSON array of UCI strings                 |
| `makePlayerMove(uci)`          | bool    | Apply move; true if legal                 |
| `makeEngineMoveAtDepth(depth)` | string  | Engine plays best move, returns UCI       |
| `getGameStatus()`              | string  | "playing" / "checkmate\_\*" / "stalemate" |
| `isInCheck()`                  | bool    | Is the side to move in check?             |

## Upcoming Features

- Improved move evaluation
- Alpha-beta optimization
- some crazy stuffs
