/*
 * chess.cpp
 * ─────────────────────────────────────────────────────────────────────────────
 * Complete chess engine compiled to WebAssembly via Emscripten.
 * Exposes a clean API to JavaScript so the game can run in any browser
 * at near-native C++ speed.
 *
 * Architecture
 * ────────────
 *  Board       – flat int[64] mailbox, castling rights, en-passant square
 *  Move gen    – pseudo-legal per-piece, then legality filter (king-in-check)
 *  Search      – minimax with alpha-beta pruning + MVV-LVA move ordering
 *  Eval        – material values + piece-square tables (PSTs)
 *  JS API      – thin wrapper functions bound with Emscripten Embind
 *
 * Piece encoding
 * ──────────────
 *   0  = EMPTY
 *   1  = wP   2  = wN   3  = wB   4  = wR   5  = wQ   6  = wK
 *   7  = bP   8  = bN   9  = bB   10 = bR   11 = bQ   12 = bK
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include <emscripten/bind.h>
#include <string>
#include <vector>
#include <algorithm>
#include <climits>
#include <cstring>
#include <random>


using namespace emscripten;

// ─────────────────────────────────────────────
// PIECE CONSTANTS & HELPERS
// ─────────────────────────────────────────────
enum Piece {
    EMPTY = 0,
    wP=1, wN=2, wB=3, wR=4, wQ=5, wK=6,
    bP=7, bN=8, bB=9, bR=10, bQ=11, bK=12
};

// Move special-case flags
enum MoveFlag {
    NORMAL      = 0,
    DOUBLE_PUSH = 1,   // pawn advances two squares
    EP_CAPTURE  = 2,   // en-passant capture
    CASTLE_KS   = 3,   // kingside castling
    CASTLE_QS   = 4,   // queenside castling
    PROMO       = 5    // pawn promotion
};

inline bool isWhite(int p)  { return p >= 1  && p <= 6;  }
inline bool isBlack(int p)  { return p >= 7  && p <= 12; }
inline bool isEmpty(int p)  { return p == 0; }

// Strip colour to get 1=P 2=N 3=B 4=R 5=Q 6=K
inline int  pieceType(int p)  { return (p > 6) ? p - 6 : p; }

// +1 for white, -1 for black
inline int  pieceColor(int p) { return isWhite(p) ? 1 : -1; }

// Board index helpers
inline int rank(int sq) { return sq / 8; }   // 0 = rank-8 (black back-rank), 7 = rank-1
inline int file(int sq) { return sq % 8; }   // 0 = a-file, 7 = h-file
inline int makeSquare(int r, int c) { return r * 8 + c; }

// ─────────────────────────────────────────────
// MOVE STRUCT
// ─────────────────────────────────────────────
struct Move {
    int from;
    int to;
    int piece;      // moving piece
    int captured;   // captured piece (EMPTY if none)
    int flag;       // MoveFlag
    int promo;      // promotion target type (1-6), 0 if not a promotion

    Move() : from(0), to(0), piece(0), captured(0), flag(NORMAL), promo(0) {}
    Move(int f, int t, int p, int cap, int fl = NORMAL, int pr = 0)
        : from(f), to(t), piece(p), captured(cap), flag(fl), promo(pr) {}
};

// ─────────────────────────────────────────────
// BOARD STATE
// ─────────────────────────────────────────────
struct Board {
    int  sq[64];              // piece on each square
    bool castleWK, castleWQ;  // white castling rights
    bool castleBK, castleBQ;  // black castling rights
    int  epSquare;            // en-passant target square, -1 if none
    int  turn;                // 1 = white, -1 = black
    std::vector<int> capturedWhite; // pieces captured by White
    std::vector<int> capturedBlack; // pieces captured by Black

    Board() { reset(); }

    void reset() {
        memset(sq, 0, sizeof(sq));

        // Black back rank (row 0 = rank 8)
        sq[0]=bR; sq[1]=bN; sq[2]=bB; sq[3]=bQ;
        sq[4]=bK; sq[5]=bB; sq[6]=bN; sq[7]=bR;
        for (int i = 8;  i < 16; i++) sq[i] = bP;

        // White back rank (row 7 = rank 1)
        for (int i = 48; i < 56; i++) sq[i] = wP;
        sq[56]=wR; sq[57]=wN; sq[58]=wB; sq[59]=wQ;
        sq[60]=wK; sq[61]=wB; sq[62]=wN; sq[63]=wR;

        castleWK = castleWQ = castleBK = castleBQ = true;
        epSquare = -1;
        turn     = 1; // white moves first
        capturedWhite.clear();
        capturedBlack.clear();
    }

    // Return the square index of the king for `color` (+1 or -1)
    int findKing(int color) const {
        int king = (color == 1) ? wK : bK;
        for (int i = 0; i < 64; i++)
            if (sq[i] == king) return i;
        return -1; // should never happen in a valid position
    }
};

// ─────────────────────────────────────────────
// ATTACK DETECTION
// Is square `sq_idx` attacked by any piece belonging to `byColor`?
// Uses reverse ray-casting: shoot rays FROM the target square.
// ─────────────────────────────────────────────
bool isAttacked(const Board& b, int sq_idx, int byColor) {
    int r = rank(sq_idx), f = file(sq_idx);

    // ── Knight attacks ─────────────────────
    int knightPiece = (byColor == 1) ? wN : bN;
    const int knightJumps[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},
                                    { 1,-2},{ 1,2},{ 2,-1},{ 2,1}};
    for (auto& j : knightJumps) {
        int nr = r + j[0], nf = f + j[1];
        if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
            if (b.sq[makeSquare(nr, nf)] == knightPiece) return true;
    }

    // ── Rook / Queen (orthogonal rays) ─────
    int rookPiece  = (byColor == 1) ? wR : bR;
    int queenPiece = (byColor == 1) ? wQ : bQ;
    const int ortho[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (auto& d : ortho) {
        int nr = r + d[0], nf = f + d[1];
        while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
            int p = b.sq[makeSquare(nr, nf)];
            if (p != EMPTY) {
                if (p == rookPiece || p == queenPiece) return true;
                break; // blocked
            }
            nr += d[0]; nf += d[1];
        }
    }

    // ── Bishop / Queen (diagonal rays) ─────
    int bishopPiece = (byColor == 1) ? wB : bB;
    const int diag[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (auto& d : diag) {
        int nr = r + d[0], nf = f + d[1];
        while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
            int p = b.sq[makeSquare(nr, nf)];
            if (p != EMPTY) {
                if (p == bishopPiece || p == queenPiece) return true;
                break;
            }
            nr += d[0]; nf += d[1];
        }
    }

    // ── Pawn attacks ───────────────────────
    // A pawn of `byColor` attacks diagonally "forward" — which means
    // looking backward from the target square.
    int pawnPiece = (byColor == 1) ? wP : bP;
    int pd = (byColor == 1) ? 1 : -1; // direction FROM which attacker pawn comes
    for (int df : {-1, 1}) {
        int nr = r + pd, nf = f + df;
        if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
            if (b.sq[makeSquare(nr, nf)] == pawnPiece) return true;
    }

    // ── King attacks ───────────────────────
    int kingPiece = (byColor == 1) ? wK : bK;
    for (int dr = -1; dr <= 1; dr++)
        for (int df = -1; df <= 1; df++) {
            if (dr == 0 && df == 0) continue;
            int nr = r + dr, nf = f + df;
            if (nr >= 0 && nr < 8 && nf >= 0 && nf < 8)
                if (b.sq[makeSquare(nr, nf)] == kingPiece) return true;
        }

    return false;
}

// ─────────────────────────────────────────────
// PSEUDO-LEGAL MOVE GENERATION
// Generates all candidate moves for the side to move.
// Does NOT check whether the move leaves the king in check —
// that filtering happens in getLegalMoves().
// ─────────────────────────────────────────────
void generatePseudoLegal(const Board& b, std::vector<Move>& moves) {
    int color = b.turn;
    int opp   = -color;

    for (int from = 0; from < 64; from++) {
        int p = b.sq[from];
        if (p == EMPTY || pieceColor(p) != color) continue;

        int pt = pieceType(p);
        int r  = rank(from);
        int f  = file(from);

        // ──────────── PAWN ────────────────────
        if (pt == 1) {
            int dir        = (color == 1) ? -1 : 1;
            int startRank  = (color == 1) ?  6 : 1;
            int promoRank  = (color == 1) ?  0 : 7;

            // Single push
            int fwd = makeSquare(r + dir, f);
            if (b.sq[fwd] == EMPTY) {
                if (rank(fwd) == promoRank) {
                    // All four promotion pieces
                    for (int pr = 2; pr <= 5; pr++) // N, B, R, Q
                        moves.push_back(Move(from, fwd, p, EMPTY, PROMO, pr));
                } else {
                    moves.push_back(Move(from, fwd, p, EMPTY, NORMAL));
                    // Double push from starting rank (both squares must be empty)
                    if (r == startRank) {
                        int dbl = makeSquare(r + 2*dir, f);
                        if (b.sq[dbl] == EMPTY)
                            moves.push_back(Move(from, dbl, p, EMPTY, DOUBLE_PUSH));
                    }
                }
            }

            // Diagonal captures (including promotion captures)
            for (int df : {-1, 1}) {
                int nf = f + df;
                if (nf < 0 || nf > 7) continue;
                int to = makeSquare(r + dir, nf);

                // Normal capture
                if (b.sq[to] != EMPTY && isBlack(b.sq[to]) == (color == 1)) {
                    int cap = b.sq[to];
                    if (rank(to) == promoRank) {
                        for (int pr = 2; pr <= 5; pr++)
                            moves.push_back(Move(from, to, p, cap, PROMO, pr));
                    } else {
                        moves.push_back(Move(from, to, p, cap, NORMAL));
                    }
                }
                // En-passant capture
                if (to == b.epSquare)
                    moves.push_back(Move(from, to, p, EMPTY, EP_CAPTURE));
            }
        }

        // ──────────── KNIGHT ──────────────────
        if (pt == 2) {
            const int jumps[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},
                                      { 1,-2},{ 1,2},{ 2,-1},{ 2,1}};
            for (auto& j : jumps) {
                int nr = r + j[0], nf = f + j[1];
                if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
                int to  = makeSquare(nr, nf);
                int cap = b.sq[to];
                if (cap == EMPTY || pieceColor(cap) == opp)
                    moves.push_back(Move(from, to, p, cap));
            }
        }

        // ──────────── BISHOP / QUEEN (diagonals) ──
        if (pt == 3 || pt == 5) {
            const int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
            for (auto& d : dirs) {
                int nr = r + d[0], nf = f + d[1];
                while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    int to  = makeSquare(nr, nf);
                    int cap = b.sq[to];
                    if (cap == EMPTY) {
                        moves.push_back(Move(from, to, p, EMPTY));
                    } else {
                        if (pieceColor(cap) == opp)
                            moves.push_back(Move(from, to, p, cap));
                        break; // blocked
                    }
                    nr += d[0]; nf += d[1];
                }
            }
        }

        // ──────────── ROOK / QUEEN (orthogonals) ──
        if (pt == 4 || pt == 5) {
            const int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
            for (auto& d : dirs) {
                int nr = r + d[0], nf = f + d[1];
                while (nr >= 0 && nr < 8 && nf >= 0 && nf < 8) {
                    int to  = makeSquare(nr, nf);
                    int cap = b.sq[to];
                    if (cap == EMPTY) {
                        moves.push_back(Move(from, to, p, EMPTY));
                    } else {
                        if (pieceColor(cap) == opp)
                            moves.push_back(Move(from, to, p, cap));
                        break;
                    }
                    nr += d[0]; nf += d[1];
                }
            }
        }

        // ──────────── KING ────────────────────
        if (pt == 6) {
            // Normal one-square moves
            for (int dr = -1; dr <= 1; dr++) {
                for (int df = -1; df <= 1; df++) {
                    if (dr == 0 && df == 0) continue;
                    int nr = r + dr, nf = f + df;
                    if (nr < 0 || nr > 7 || nf < 0 || nf > 7) continue;
                    int to  = makeSquare(nr, nf);
                    int cap = b.sq[to];
                    if (cap == EMPTY || pieceColor(cap) == opp)
                        moves.push_back(Move(from, to, p, cap));
                }
            }

            // Castling (only checks squares are empty; legality filter
            // will reject moves through check)
            if (color == 1) {
                if (b.castleWK && b.sq[61]==EMPTY && b.sq[62]==EMPTY && b.sq[63]==wR)
                    moves.push_back(Move(from, 62, p, EMPTY, CASTLE_KS));
                if (b.castleWQ && b.sq[59]==EMPTY && b.sq[58]==EMPTY
                               && b.sq[57]==EMPTY && b.sq[56]==wR)
                    moves.push_back(Move(from, 58, p, EMPTY, CASTLE_QS));
            } else {
                if (b.castleBK && b.sq[5]==EMPTY && b.sq[6]==EMPTY && b.sq[7]==bR)
                    moves.push_back(Move(from, 6, p, EMPTY, CASTLE_KS));
                if (b.castleBQ && b.sq[3]==EMPTY && b.sq[2]==EMPTY
                               && b.sq[1]==EMPTY && b.sq[0]==bR)
                    moves.push_back(Move(from, 2, p, EMPTY, CASTLE_QS));
            }
        }
    }
}

// ─────────────────────────────────────────────
// APPLY MOVE
// Returns a new Board state — never mutates the original.
// ─────────────────────────────────────────────
Board applyMove(const Board& b, const Move& mv) {
    Board nb = b;
    int color = b.turn;

    // Determine the piece that lands on the destination square
    // (handle promotion: white promotes to wN-wQ, black to bN-bQ)
    int landingPiece;
    if (mv.flag == PROMO) {
        landingPiece = (color == 1) ? mv.promo : mv.promo + 6;
    } else {
        landingPiece = mv.piece;
    }

    nb.sq[mv.to]   = landingPiece;
    nb.sq[mv.from] = EMPTY;

    // En-passant: remove the captured pawn (it sits beside the landing square)
    if (mv.flag == EP_CAPTURE) {
        int capturedRank = rank(mv.to) + (color == 1 ? 1 : -1);
        int epCapturedPiece = nb.sq[makeSquare(capturedRank, file(mv.to))];
        nb.sq[makeSquare(capturedRank, file(mv.to))] = EMPTY;
        if (color == 1) nb.capturedWhite.push_back(epCapturedPiece);
        else            nb.capturedBlack.push_back(epCapturedPiece);
    } else if (mv.captured != EMPTY) {
        if (color == 1) nb.capturedWhite.push_back(mv.captured);
        else            nb.capturedBlack.push_back(mv.captured);
    }

    // Castling: slide the rook to its new square
    if (mv.flag == CASTLE_KS) {
        if (color == 1) { nb.sq[61] = wR; nb.sq[63] = EMPTY; }
        else            { nb.sq[5]  = bR; nb.sq[7]  = EMPTY; }
    }
    if (mv.flag == CASTLE_QS) {
        if (color == 1) { nb.sq[59] = wR; nb.sq[56] = EMPTY; }
        else            { nb.sq[3]  = bR; nb.sq[0]  = EMPTY; }
    }

    // Set en-passant target for the opponent's next ply
    nb.epSquare = (mv.flag == DOUBLE_PUSH)
        ? makeSquare(rank(mv.to) + (color == 1 ? 1 : -1), file(mv.to))
        : -1;

    // Update castling rights when a king or rook moves / is captured
    if (mv.piece == wK) { nb.castleWK = nb.castleWQ = false; }
    if (mv.piece == bK) { nb.castleBK = nb.castleBQ = false; }
    if (mv.from == 63 || mv.to == 63) nb.castleWK = false;
    if (mv.from == 56 || mv.to == 56) nb.castleWQ = false;
    if (mv.from ==  7 || mv.to ==  7) nb.castleBK = false;
    if (mv.from ==  0 || mv.to ==  0) nb.castleBQ = false;

    nb.turn = -color; // flip side to move
    return nb;
}

// ─────────────────────────────────────────────
// LEGAL MOVE GENERATION
// Filters pseudo-legal moves that leave the moving side in check.
// Also validates castling (king must not pass through check).
// ─────────────────────────────────────────────
std::vector<Move> getLegalMoves(const Board& b) {
    std::vector<Move> pseudo, legal;
    pseudo.reserve(64);
    generatePseudoLegal(b, pseudo);

    int color = b.turn;
    int opp   = -color;

    for (const Move& mv : pseudo) {
        // Castling: king must not start in, pass through, or land in check
        if (mv.flag == CASTLE_KS || mv.flag == CASTLE_QS) {
            int kp  = mv.from;
            int mid = (mv.to > kp) ? kp + 1 : kp - 1;
            if (isAttacked(b, kp,     opp) ||
                isAttacked(b, mid,    opp) ||
                isAttacked(b, mv.to,  opp)) continue;
        }

        // Simulate the move and check whether the king is still safe
        Board nb = applyMove(b, mv);
        int   kp = nb.findKing(color);
        if (kp >= 0 && !isAttacked(nb, kp, opp))
            legal.push_back(mv);
    }

    return legal;
}

// ─────────────────────────────────────────────
// STATIC EVALUATION
// Returns a score in centipawns from White's perspective.
//   positive → White is better
//   negative → Black is better
// ─────────────────────────────────────────────
const int MAT[7] = {0, 100, 320, 330, 500, 900, 20000};

// Piece-square tables indexed [row][file]
// Row 0 = rank 8 (Black's back rank from White's perspective).
// For Black pieces the row is mirrored: use (7 - row).
const int PST_P[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0 },
    { 50, 50, 50, 50, 50, 50, 50, 50 },
    { 10, 10, 20, 30, 30, 20, 10, 10 },
    {  5,  5, 10, 25, 25, 10,  5,  5 },
    {  0,  0,  0, 20, 20,  0,  0,  0 },
    {  5, -5,-10,  0,  0,-10, -5,  5 },
    {  5, 10, 10,-20,-20, 10, 10,  5 },
    {  0,  0,  0,  0,  0,  0,  0,  0 }
};
const int PST_N[8][8] = {
    {-50,-40,-30,-30,-30,-30,-40,-50 },
    {-40,-20,  0,  0,  0,  0,-20,-40 },
    {-30,  0, 10, 15, 15, 10,  0,-30 },
    {-30,  5, 15, 20, 20, 15,  5,-30 },
    {-30,  0, 15, 20, 20, 15,  0,-30 },
    {-30,  5, 10, 15, 15, 10,  5,-30 },
    {-40,-20,  0,  5,  5,  0,-20,-40 },
    {-50,-40,-30,-30,-30,-30,-40,-50 }
};
const int PST_B[8][8] = {
    {-20,-10,-10,-10,-10,-10,-10,-20 },
    {-10,  0,  0,  0,  0,  0,  0,-10 },
    {-10,  0,  5, 10, 10,  5,  0,-10 },
    {-10,  5,  5, 10, 10,  5,  5,-10 },
    {-10,  0, 10, 10, 10, 10,  0,-10 },
    {-10, 10, 10, 10, 10, 10, 10,-10 },
    {-10,  5,  0,  0,  0,  0,  5,-10 },
    {-20,-10,-10,-10,-10,-10,-10,-20 }
};
const int PST_R[8][8] = {
    {  0,  0,  0,  0,  0,  0,  0,  0 },
    {  5, 10, 10, 10, 10, 10, 10,  5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    { -5,  0,  0,  0,  0,  0,  0, -5 },
    {  0,  0,  0,  5,  5,  0,  0,  0 }
};
const int PST_Q[8][8] = {
    {-20,-10,-10, -5, -5,-10,-10,-20 },
    {-10,  0,  0,  0,  0,  0,  0,-10 },
    {-10,  0,  5,  5,  5,  5,  0,-10 },
    { -5,  0,  5,  5,  5,  5,  0, -5 },
    {  0,  0,  5,  5,  5,  5,  0, -5 },
    {-10,  5,  5,  5,  5,  5,  0,-10 },
    {-10,  0,  5,  0,  0,  0,  0,-10 },
    {-20,-10,-10, -5, -5,-10,-10,-20 }
};
const int PST_K[8][8] = {
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-30,-40,-40,-50,-50,-40,-40,-30 },
    {-20,-30,-30,-40,-40,-30,-30,-20 },
    {-10,-20,-20,-20,-20,-20,-20,-10 },
    { 20, 20,  0,  0,  0,  0, 20, 20 },
    { 20, 30, 10,  0,  0, 10, 30, 20 }
};

// Dispatch PST lookup by piece type
const int* PST_TABLE[7] = {
    nullptr,
    (const int*)PST_P, (const int*)PST_N, (const int*)PST_B,
    (const int*)PST_R, (const int*)PST_Q, (const int*)PST_K
};

inline int getPST(int pt, int r, int f) {
    return PST_TABLE[pt] ? PST_TABLE[pt][r * 8 + f] : 0;
}

int evaluate(const Board& b) {
    int score = 0;
    for (int i = 0; i < 64; i++) {
        int p = b.sq[i];
        if (p == EMPTY) continue;
        int pt  = pieceType(p);
        int r   = isWhite(p) ? rank(i) : 7 - rank(i); // mirror for black
        int val = MAT[pt] + getPST(pt, r, file(i));
        score  += isWhite(p) ? val : -val;
    }
    return score;
}

// ─────────────────────────────────────────────
// MOVE ORDERING — MVV-LVA
// Score captures highly so alpha-beta prunes more branches.
// MVV = Most Valuable Victim, LVA = Least Valuable Attacker
// ─────────────────────────────────────────────
inline int moveScore(const Move& mv) {
    if (mv.captured != EMPTY)
        return 10 * MAT[pieceType(mv.captured)] - MAT[pieceType(mv.piece)];
    if (mv.flag == PROMO)
        return MAT[mv.promo];
    return 0;
}

void orderMoves(std::vector<Move>& moves) {
    std::sort(moves.begin(), moves.end(),
        [](const Move& a, const Move& b) {
            return moveScore(a) > moveScore(b);
        });
}

// ─────────────────────────────────────────────
// MINIMAX WITH ALPHA-BETA PRUNING
//
// maximizing = true  → White's turn (wants highest score)
// maximizing = false → Black's turn (wants lowest score)
//
// depth 5 is easily feasible in C++/WASM (~1500–1700 ELO strength).
// Increasing to depth 6–7 is viable with a transposition table.
// ─────────────────────────────────────────────
int minimax(const Board& b, int depth, int alpha, int beta, bool maximizing) {
    if (depth == 0) return evaluate(b);

    auto moves = getLegalMoves(b);

    // Terminal node: checkmate or stalemate
    if (moves.empty()) {
        int kingPos = b.findKing(b.turn);
        if (kingPos >= 0 && isAttacked(b, kingPos, -b.turn))
            return maximizing ? (-100000 - depth) : (100000 + depth); // prefer faster mates
        return 0; // stalemate
    }

    orderMoves(moves);

    if (maximizing) {
        int best = INT_MIN;
        for (const Move& mv : moves) {
            Board nb = applyMove(b, mv);
            best  = std::max(best, minimax(nb, depth - 1, alpha, beta, false));
            alpha = std::max(alpha, best);
            if (beta <= alpha) break; // beta cut-off
        }
        return best;
    } else {
        int best = INT_MAX;
        for (const Move& mv : moves) {
            Board nb = applyMove(b, mv);
            best = std::min(best, minimax(nb, depth - 1, alpha, beta, true));
            beta = std::min(beta, best);
            if (beta <= alpha) break; // alpha cut-off
        }
        return best;
    }
}

// ─────────────────────────────────────────────
// GLOBAL GAME STATE
// ─────────────────────────────────────────────
Board gBoard;

// ─────────────────────────────────────────────
// COORDINATE UTILITIES
// ─────────────────────────────────────────────

// Square index → algebraic notation  (e.g. 60 → "e1")
std::string sqToAlg(int sq) {
    std::string s;
    s += (char)('a' + file(sq));
    s += (char)('1' + (7 - rank(sq)));
    return s;
}

// Move → UCI string  (e.g. "e2e4", "e7e8q")
std::string moveToUCI(const Move& mv) {
    std::string s = sqToAlg(mv.from) + sqToAlg(mv.to);
    if (mv.flag == PROMO) {
        const char promoChars[] = {0, 0, 'n', 'b', 'r', 'q', 0};
        s += promoChars[mv.promo];
    }
    return s;
}

// Algebraic notation → square index  (e.g. "e1" → 60)
int algToSq(const std::string& s) {
    if (s.size() < 2) return -1;
    int f = s[0] - 'a';
    int r = 7 - (s[1] - '1');
    return makeSquare(r, f);
}

// ─────────────────────────────────────────────
// JAVASCRIPT API FUNCTIONS
// These are bound to JS via Emscripten Embind.
// ─────────────────────────────────────────────

// Reset the board to the starting position
void initGame() {
    gBoard.reset();
}

// Return the entire board state as a compact JSON string.
// The JS UI parses this to render the board.
std::string getBoardJSON() {
    std::string json = "{\"squares\":[";
    for (int i = 0; i < 64; i++) {
        json += std::to_string(gBoard.sq[i]);
        if (i < 63) json += ",";
    }
    json += "]";
    json += ",\"turn\":"    + std::to_string(gBoard.turn);
    json += ",\"ep\":"      + std::to_string(gBoard.epSquare);
    json += ",\"castleWK\":" + std::string(gBoard.castleWK ? "true" : "false");
    json += ",\"castleWQ\":" + std::string(gBoard.castleWQ ? "true" : "false");
    json += ",\"castleBK\":" + std::string(gBoard.castleBK ? "true" : "false");
    json += ",\"castleBQ\":" + std::string(gBoard.castleBQ ? "true" : "false");

    json += ",\"capturedWhite\":[";
    for (size_t i = 0; i < gBoard.capturedWhite.size(); i++) {
        json += std::to_string(gBoard.capturedWhite[i]);
        if (i < gBoard.capturedWhite.size() - 1) json += ",";
    }
    json += "]";

    json += ",\"capturedBlack\":[";
    for (size_t i = 0; i < gBoard.capturedBlack.size(); i++) {
        json += std::to_string(gBoard.capturedBlack[i]);
        if (i < gBoard.capturedBlack.size() - 1) json += ",";
    }
    json += "]";

    json += "}";
    return json;
}

// Return all legal moves from a given square as a JSON array of UCI strings.
// Used by the UI to display move hints (dots/rings) when the player selects a piece.
std::string getLegalMovesFromSquare(int fromSq) {
    auto moves = getLegalMoves(gBoard);
    std::string json = "[";
    bool first = true;
    for (const Move& mv : moves) {
        if (mv.from == fromSq) {
            if (!first) json += ",";
            json += "\"" + moveToUCI(mv) + "\"";
            first = false;
        }
    }
    json += "]";
    return json;
}

// Apply a player's move (given as a UCI string).
// Returns true if the move was legal and was applied; false otherwise.
bool makePlayerMove(const std::string& uci) {
    auto moves = getLegalMoves(gBoard);
    for (const Move& mv : moves) {
        if (moveToUCI(mv) == uci) {
            gBoard = applyMove(gBoard, mv);
            return true;
        }
    }
    return false;
}

// Find and play the engine's best move at the given depth.
// If beginner is true, it performs a depth 1 search with added random noise
// to each move's evaluation to simulate human-like blunders and miscalculations.
std::string makeEngineMove(int depth, bool beginner) {
    auto moves = getLegalMoves(gBoard);
    if (moves.empty()) return "";

    static std::mt19937 rng(std::random_device{}());

    if (beginner) {
        // Stochastic evaluation for beginners (~500 Elo)
        // Instead of picking the best move, we add significant noise to each move's score.
        std::uniform_int_distribution<int> noiseDist(-300, 300);
        
        bool maximizing = (gBoard.turn == 1);
        Move best = moves[0];
        int  bestScore = maximizing ? INT_MIN : INT_MAX;

        for (const Move& mv : moves) {
            Board nb = applyMove(gBoard, mv);
            // Evaluate at depth 0 (static eval after move) + noise
            int score = evaluate(nb) + noiseDist(rng);
            
            if (maximizing ? (score > bestScore) : (score < bestScore)) {
                bestScore = score;
                best      = mv;
            }
        }
        gBoard = applyMove(gBoard, best);
        return moveToUCI(best);
    }

    orderMoves(moves);

    bool maximizing = (gBoard.turn == 1);
    Move best = moves[0];
    int  bestScore = maximizing ? INT_MIN : INT_MAX;

    for (const Move& mv : moves) {
        Board nb    = applyMove(gBoard, mv);
        int   score = minimax(nb, depth - 1, INT_MIN, INT_MAX, !maximizing);
        if (maximizing ? (score > bestScore) : (score < bestScore)) {
            bestScore = score;
            best      = mv;
        }
    }

    gBoard = applyMove(gBoard, best);
    return moveToUCI(best);
}

// Deprecated: kept for backward compatibility if needed, but ui.js should use makeEngineMove
std::string makeEngineMoveAtDepth(int depth) {
    return makeEngineMove(depth, false);
}


// Game result: "playing" | "checkmate_white_wins" | "checkmate_black_wins" | "stalemate"
std::string getGameStatus() {
    auto moves = getLegalMoves(gBoard);
    if (!moves.empty()) return "playing";
    int kingPos = gBoard.findKing(gBoard.turn);
    if (kingPos >= 0 && isAttacked(gBoard, kingPos, -gBoard.turn))
        return gBoard.turn == 1 ? "checkmate_black_wins" : "checkmate_white_wins";
    return "stalemate";
}

// Is the side to move currently in check?
bool isInCheck() {
    int kp = gBoard.findKing(gBoard.turn);
    return kp >= 0 && isAttacked(gBoard, kp, -gBoard.turn);
}

// ─────────────────────────────────────────────
// EMSCRIPTEN EMBIND BINDINGS
// Exposes the C++ API functions to JavaScript.
// ─────────────────────────────────────────────
EMSCRIPTEN_BINDINGS(chess_engine) {
    emscripten::function("initGame",                &initGame);
    emscripten::function("getBoardJSON",            &getBoardJSON);
    emscripten::function("getLegalMovesFromSquare", &getLegalMovesFromSquare);
    emscripten::function("makePlayerMove",          &makePlayerMove);
    emscripten::function("makeEngineMove",          &makeEngineMove);
    emscripten::function("makeEngineMoveAtDepth",   &makeEngineMoveAtDepth);
    emscripten::function("getGameStatus",           &getGameStatus);

    emscripten::function("isInCheck",               &isInCheck);
}
