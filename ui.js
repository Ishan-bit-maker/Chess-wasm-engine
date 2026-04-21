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
  '♟',  // 1  wP (solid)
  '♞',  // 2  wN
  '♝',  // 3  wB
  '♜',  // 4  wR
  '♛',  // 5  wQ
  '♚',  // 6  wK
  '♟',  // 7  bP (solid)
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
let prevBoard = null;     // previous board state for animation tracking
let flipped   = false;    // true = view from Black's side
let thinking  = false;    // true while C++ engine is computing

// ─────────────────────────────────────────────
// SOUNDS
// ─────────────────────────────────────────────
const sounds = {
  move:    new Audio('https://lichess.org/assets/sound/standard/Move.mp3'),
  capture: new Audio('https://lichess.org/assets/sound/standard/Capture.mp3'),
  check:   new Audio('https://lichess.org/assets/sound/standard/Check.mp3'),
  notify:  new Audio('https://lichess.org/assets/sound/standard/GenericNotify.mp3')
};

function playSound(type) {
  if (sounds[type]) {
    sounds[type].currentTime = 0;
    sounds[type].play().catch(() => {}); // catch blocks for browsers that block auto-play
  }
}

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
// ─────────────────────────────────────────────
// RENDERING (Animated & Optimized)
// ─────────────────────────────────────────────
function initBoard() {
  const boardEl = document.getElementById('board');
  if (boardEl.children.length === 64) return;
  
  boardEl.innerHTML = ''; // clear any placeholders
  for (let i = 0; i < 64; i++) {
    const sq = document.createElement('div');
    sq.className = 'sq';
    sq.dataset.sq = i;
    sq.addEventListener('click', () => handleSquareClick(i));
    boardEl.appendChild(sq);
  }
}

function render() {
  if (!board || !engine) return;
  initBoard();

  const boardEl = document.getElementById('board');
  const ranksEl = document.getElementById('ranks');
  const filesEl = document.getElementById('files');

  const rows = flipped ? [7,6,5,4,3,2,1,0] : [0,1,2,3,4,5,6,7];
  const cols = flipped ? [7,6,5,4,3,2,1,0] : [0,1,2,3,4,5,6,7];

  const inCheck   = !thinking && engine.isInCheck();
  const kingPiece = (board.turn === 1) ? 6 : 12;

  // Track if we should animate a move
  let animatedPiece = null;
  let moveFromRect = null;

  // 1. Update Board Squares
  rows.forEach((r, rowIdx) => {
    cols.forEach((c, colIdx) => {
      const i = r * 8 + c;
      const sqEl = boardEl.children[rowIdx * 8 + colIdx];
      sqEl.dataset.sq = i;
      
      const piece = board.squares[i];
      const isLight = (r + c) % 2 === 0;

      // Update Classes
      sqEl.className = `sq ${isLight ? 'sq-light' : 'sq-dark'}`;
      if (selected === i) sqEl.classList.add('is-selected');
      if (i === lastFrom || i === lastTo) sqEl.classList.add('is-last-move');
      if (inCheck && piece === kingPiece) sqEl.classList.add('is-in-check');

      // Update Piece Element
      let pieceEl = sqEl.querySelector('.piece:not(.ghost)');
      const sideClass = piece <= 6 ? 'white-piece' : 'black-piece';
      
      // Remove any existing ghost
      const oldGhost = sqEl.querySelector('.piece.ghost');
      if (oldGhost) oldGhost.remove();

      if (piece === 0) {
        if (pieceEl) pieceEl.remove();
        
        // Add ghost piece at starting square of last move
        if (i === lastFrom && prevBoard && prevBoard.squares[i] !== 0) {
          const ghost = document.createElement('span');
          const ghostPiece = prevBoard.squares[i];
          const ghostSide = ghostPiece <= 6 ? 'white-piece' : 'black-piece';
          ghost.className = `piece ghost ${ghostSide}`;
          ghost.textContent = GLYPHS[ghostPiece];
          sqEl.appendChild(ghost);
        }
      } else {
        const isNewPiece = !pieceEl;
        const isUserTurn = board.turn === 1 && !thinking;
        const isPlayable = isUserTurn && piece >= 1 && piece <= 6;
        
        if (isNewPiece) {
          pieceEl = document.createElement('span');
          pieceEl.className = `piece ${sideClass} ${isPlayable ? 'is-playable' : ''}`;
          sqEl.appendChild(pieceEl);
        } else {
          pieceEl.className = `piece ${sideClass} ${isPlayable ? 'is-playable' : ''}`;
        }
        pieceEl.textContent = GLYPHS[piece];

        // ANIMATION LOGIC: If this piece just moved here
        if (i === lastTo && lastFrom !== null && prevBoard && prevBoard.squares[lastFrom] === piece) {
          animatedPiece = pieceEl;
          const fromRowIdx = rows.indexOf(Math.floor(lastFrom / 8));
          const fromColIdx = cols.indexOf(lastFrom % 8);
          const fromSqEl = boardEl.children[fromRowIdx * 8 + fromColIdx];
          moveFromRect = fromSqEl.getBoundingClientRect();
        }
      }

      // Update Hints
      const isHint = hints.includes(i);
      let hintEl = sqEl.querySelector('.hint-dot, .hint-cap');
      if (isHint) {
        const hintType = piece !== 0 ? 'hint-cap' : 'hint-dot';
        if (!hintEl || !hintEl.classList.contains(hintType)) {
          if (hintEl) hintEl.remove();
          hintEl = document.createElement('div');
          hintEl.className = hintType;
          sqEl.appendChild(hintEl);
        }
      } else if (hintEl) {
        hintEl.remove();
      }
    });
  });

  // Execute Piece Slide Animation (FLIP)
  if (animatedPiece && moveFromRect) {
    const toRect = animatedPiece.getBoundingClientRect();
    const dx = moveFromRect.left - toRect.left;
    const dy = moveFromRect.top - toRect.top;

    animatedPiece.style.transition = 'none';
    animatedPiece.style.transform = `translate(${dx}px, ${dy}px)`;
    
    requestAnimationFrame(() => {
      animatedPiece.style.transition = 'transform 0.25s cubic-bezier(0.4, 0, 0.2, 1)';
      animatedPiece.style.transform = 'translate(0, 0)';
    });
  }

  // 2. Labels
  ranksEl.innerHTML = rows.map(r => `<span>${r + 1}</span>`).join('');
  filesEl.innerHTML = cols.map(c => `<span>${String.fromCharCode(97 + c)}</span>`).join('');

  prevBoard = JSON.parse(JSON.stringify(board)); // deep copy for next render
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
      // Check for capture
      const isCapture = board.squares[i] !== 0;
      
      // Default to queen promotion if multiple promotion choices exist
      const uci = movingMoves.find(u => u.length === 5 && u[4] === 'q')
               || movingMoves[0];

      engine.makePlayerMove(uci);
      syncBoard();
      lastFrom = selected;
      lastTo   = i;
      selected = null;
      hints    = [];
      
      // Sound feedback
      if (engine.isInCheck()) playSound('check');
      else if (isCapture) playSound('capture');
      else playSound('move');

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
      const fromI = (algFrom.charCodeAt(0) - 97) + (7 - (parseInt(algFrom[1]) - 1)) * 8;
      const toI   = (algTo.charCodeAt(0)   - 97) + (7 - (parseInt(algTo[1])   - 1)) * 8;
      
      const isCapture = board.squares[toI] !== 0;

      lastFrom = fromI;
      lastTo   = toI;

      syncBoard();
      
      if (engine.isInCheck()) playSound('check');
      else if (isCapture) playSound('capture');
      else playSound('move');
    }

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
