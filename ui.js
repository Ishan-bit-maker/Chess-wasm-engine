/**
 * ui.js  —  Chess Engine (C++ / WebAssembly)
 * ─────────────────────────────────────────────────────────────────────────────
 * This file handles everything on the JavaScript side:
 *   1. Load the Emscripten-generated WASM module asynchronously
 *   2. Maintain a thin game-state mirror for rendering
 *   3. Translate player clicks into UCI move strings
 *   4. Delegate search to the C++ engine via the bound API
 *
 * C++ API (exposed via Emscripten Embind in chess.cpp):
 *   engine.initGame()                    — reset board to start position
 *   engine.getBoardJSON()                — board state as JSON string
 *   engine.getLegalMovesFromSquare(sq)   — JSON array of UCI strings from sq
 *   engine.makePlayerMove(uci)           — apply a move; returns bool
 *   engine.makeEngineMoveAtDepth(depth)  — engine plays best move; returns UCI
 *   engine.getGameStatus()               — "playing" | "checkmate_*" | "stalemate"
 *   engine.isInCheck()                   — is the side to move in check?
 *
 * Piece encoding (matches chess.cpp):
 *   0=empty  1=wP  2=wN  3=wB  4=wR  5=wQ  6=wK
 *            7=bP  8=bN  9=bB  10=bR 11=bQ 12=bK
 * ─────────────────────────────────────────────────────────────────────────────
 */

'use strict';

// ─────────────────────────────────────────────
// PIECE RENDERING
// Index matches the piece encoding in chess.cpp.
// ─────────────────────────────────────────────
const GLYPHS = [
  '',    // 0  empty
  '♙',  // 1  wP
  '♘',  // 2  wN
  '♗',  // 3  wB
  '♖',  // 4  wR
  '♕',  // 5  wQ
  '♔',  // 6  wK
  '♟',  // 7  bP
  '♞',  // 8  bN
  '♝',  // 9  bB
  '♜',  // 10 bR
  '♛',  // 11 bQ
  '♚',  // 12 bK
];

// ─────────────────────────────────────────────
// COORDINATE HELPERS
// The C++ engine uses the same index scheme:
//   index = rank * 8 + file
//   rank  0 = rank-8 (top/black back-rank)
//   rank  7 = rank-1 (bottom/white back-rank)
// ─────────────────────────────────────────────

/** Square index → algebraic string, e.g. 60 → "e1" */
function sqToAlg(sq) {
  const f = sq % 8;
  const r = Math.floor(sq / 8);
  return String.fromCharCode(97 + f) + String(8 - r);
}

/**
 * Extract the destination square from a UCI string.
 * e.g. "e2e4" → destSq = algToSq("e4") = 28
 */
function uciDest(uci) {
  const f = uci.charCodeAt(2) - 97;
  const r = 7 - (parseInt(uci[3]) - 1);
  return r * 8 + f;
}

// ─────────────────────────────────────────────
// UI STATE
// A thin mirror of game state used purely for
// rendering — the source of truth lives in C++.
// ─────────────────────────────────────────────
let engine    = null;     // the Emscripten module instance
let board     = null;     // parsed board state from C++
let selected  = null;     // currently selected square index (or null)
let hints     = [];       // legal destination squares for selected piece
let lastFrom  = null;     // source square of most recent move (for highlight)
let lastTo    = null;     // dest  square of most recent move
let flipped   = false;    // true = view from Black's side
let thinking  = false;    // true while C++ engine is computing

// ─────────────────────────────────────────────
// BOARD STATE SYNC
// Fetch the current board state from C++ and
// cache it as a parsed JS object.
// ─────────────────────────────────────────────
function syncBoard() {
  board = JSON.parse(engine.getBoardJSON());
}

// ─────────────────────────────────────────────
// RENDERING
// Builds the board DOM from scratch on every
// state change. Simple and reliable.
// ─────────────────────────────────────────────
function render() {
  if (!board) return;

  const boardEl = document.getElementById('board');
  const ranksEl = document.getElementById('ranks');
  const filesEl = document.getElementById('files');

  // Determine square ordering based on board orientation
  const rows = flipped ? [0,1,2,3,4,5,6,7] : [7,6,5,4,3,2,1,0];
  const cols = flipped ? [7,6,5,4,3,2,1,0] : [0,1,2,3,4,5,6,7];

  const inCheck  = !thinking && engine && engine.isInCheck();
  const kingPiece = (board.turn === 1) ? 6 : 12; // wK or bK

  // ── Squares ────────────────────────────────
  let squaresHTML = '';
  rows.forEach(r => {
    cols.forEach(c => {
      const i      = r * 8 + c;
      const piece  = board.squares[i];
      const isLight = (r + c) % 2 === 0;

      // Background colour: light/dark + highlights
      let bg = isLight ? '#f0d9b5' : '#b58863';    // classic wooden board
      if (selected === i) {
        bg = '#6dc16d';                             // selected piece: green
      } else if (i === lastFrom || i === lastTo) {
        bg = isLight ? '#cdd16e' : '#aaa23a';       // last move: yellow
      }
      if (inCheck && piece === kingPiece) {
        bg = '#e06060';                             // king in check: red
      }

      const isHint   = hints.includes(i);
      const hasPiece = piece !== 0;

      squaresHTML += `<div class="sq" style="background:${bg}" data-sq="${i}">`;
      if (piece)   squaresHTML += `<span>${GLYPHS[piece]}</span>`;
      if (isHint)  squaresHTML += hasPiece
        ? `<div class="hint-cap"></div>`
        : `<div class="hint-dot"></div>`;
      squaresHTML += `</div>`;
    });
  });
  boardEl.innerHTML = squaresHTML;

  // ── Rank labels ─────────────────────────────
  ranksEl.innerHTML = rows.map(r => `<span>${r + 1}</span>`).join('');

  // ── File labels ─────────────────────────────
  filesEl.innerHTML = cols.map(c =>
    `<span>${String.fromCharCode(97 + c)}</span>`
  ).join('');

  // ── Re-attach click listeners ────────────────
  boardEl.querySelectorAll('.sq').forEach(el =>
    el.addEventListener('click', () => handleSquareClick(+el.dataset.sq))
  );

  // ── Status text ─────────────────────────────
  updateStatus(inCheck);
}

function updateStatus(inCheck) {
  const status = engine ? engine.getGameStatus() : 'loading';
  const el     = document.getElementById('status');

  if (status === 'checkmate_black_wins') { el.textContent = 'Checkmate — Engine wins!'; return; }
  if (status === 'checkmate_white_wins') { el.textContent = 'Checkmate — You win!';     return; }
  if (status === 'stalemate')            { el.textContent = 'Stalemate — Draw!';         return; }
  if (thinking)  { el.textContent = 'Engine is thinking…';               return; }
  if (inCheck)   { el.textContent = 'Check! Your turn.';                 return; }

  el.textContent = board.turn === 1 ? 'Your turn (White)' : "Engine's turn…";
}

// ─────────────────────────────────────────────
// PLAYER INPUT
// ─────────────────────────────────────────────
function handleSquareClick(i) {
  if (!engine || thinking) return;
  if (engine.getGameStatus() !== 'playing') return;
  if (board.turn !== 1) return; // not player's turn

  const piece = board.squares[i];

  // ── If a piece is already selected, try to move ──
  if (selected !== null) {
    // Find matching hint moves (there may be multiple for promotions)
    const movingMoves = JSON.parse(engine.getLegalMovesFromSquare(selected))
      .filter(uci => uciDest(uci) === i);

    if (movingMoves.length > 0) {
      // Default to queen promotion if multiple promotion choices exist
      const uci = movingMoves.find(u => u.length === 5 && u[4] === 'q')
               || movingMoves[0];

      engine.makePlayerMove(uci);
      syncBoard();
      lastFrom = selected;
      lastTo   = i;
      selected = null;
      hints    = [];
      render();

      // Trigger engine response after a brief repaint delay
      if (engine.getGameStatus() === 'playing') {
        scheduleEngineMove();
      }
      return;
    }
  }

  // ── Select a white piece ─────────────────────
  if (piece >= 1 && piece <= 6) {
    selected = i;
    hints    = JSON.parse(engine.getLegalMovesFromSquare(i)).map(uciDest);
  } else {
    selected = null;
    hints    = [];
  }

  render();
}

// ─────────────────────────────────────────────
// ENGINE MOVE
// Runs the C++ minimax search off the main
// thread using setTimeout so the browser can
// repaint the "thinking…" status first.
// ─────────────────────────────────────────────
function scheduleEngineMove() {
  thinking = true;
  render(); // show "Engine is thinking…"

  setTimeout(() => {
    const depth = parseInt(document.getElementById('depth-select').value, 10);
    const uci   = engine.makeEngineMoveAtDepth(depth);

    if (uci) {
      // Parse from/to from the UCI string for highlighting
      const algFrom = uci.slice(0, 2);
      const algTo   = uci.slice(2, 4);
      lastFrom = (algFrom.charCodeAt(0) - 97) + (7 - (parseInt(algFrom[1]) - 1)) * 8;
      lastTo   = (algTo.charCodeAt(0)   - 97) + (7 - (parseInt(algTo[1])   - 1)) * 8;
    }

    syncBoard();
    thinking = false;
    render();
  }, 30); // short delay lets the browser repaint before the synchronous search
}

// ─────────────────────────────────────────────
// CONTROLS
// ─────────────────────────────────────────────
function newGame() {
  if (!engine) return;
  engine.initGame();
  syncBoard();
  selected = null;
  hints    = [];
  lastFrom = null;
  lastTo   = null;
  thinking = false;
  render();
}

function flipBoard() {
  flipped = !flipped;
  render();
}

document.getElementById('btn-new').addEventListener('click',  newGame);
document.getElementById('btn-flip').addEventListener('click', flipBoard);

// ─────────────────────────────────────────────
// WASM MODULE BOOTSTRAP
//
// ChessEngine() is a factory function created by Emscripten
// (from chess.js). It returns a Promise that resolves to the
// module object once the WASM binary is downloaded and compiled.
//
// Build command:
//   em++ chess.cpp -O3 -o chess.js           \
//     --bind                                  \
//     -s WASM=1                               \
//     -s MODULARIZE=1                         \
//     -s EXPORT_NAME="ChessEngine"            \
//     -s ALLOW_MEMORY_GROWTH=1
// ─────────────────────────────────────────────
ChessEngine().then(module => {
  engine = module;
  engine.initGame();
  syncBoard();

  // Hide the loading overlay
  document.getElementById('loader').classList.add('hidden');

  render();
}).catch(err => {
  document.getElementById('loader').innerHTML =
    `<div id="loader-box" style="color:#f88">
       <p>Failed to load WebAssembly engine.</p>
       <p style="font-size:11px;margin-top:8px;">${err.message}</p>
     </div>`;
  console.error('WASM load error:', err);
});
