#include "evaluate.h"
#include "position.h"
#include "nnue/network.h"
#include <algorithm>
#include <sstream>

namespace blitz {
namespace Eval {

namespace {

struct Score {
    int mg = 0, eg = 0;
    constexpr Score() = default;
    constexpr Score(int m, int e) : mg(m), eg(e) {}
    Score& operator+=(Score s) { mg += s.mg; eg += s.eg; return *this; }
    Score& operator-=(Score s) { mg -= s.mg; eg -= s.eg; return *this; }
    friend constexpr Score operator-(Score a, Score b) { return { a.mg - b.mg, a.eg - b.eg }; }
    friend constexpr Score operator*(int n, Score s) { return { n * s.mg, n * s.eg }; }
};

constexpr int MgValue[7] = { 0, 82, 337, 365, 477, 1025, 0 };
constexpr int EgValue[7] = { 0, 94, 281, 297, 512,  936, 0 };

constexpr int PawnMg[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     98, 134,  61,  95,  68, 126,  34, -11,
     -6,   7,  26,  31,  65,  56,  25, -20,
    -14,  13,   6,  21,  23,  12,  17, -23,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -35,  -1, -20, -23, -15,  24,  38, -22,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int PawnEg[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int KnightMg[64] = {
   -167, -89, -34, -49,  61, -97, -15,-107,
    -73, -41,  72,  36,  23,  62,   7, -17,
    -47,  60,  37,  65,  84, 129,  73,  44,
     -9,  17,  19,  53,  37,  69,  18,  22,
    -13,   4,  16,  13,  28,  19,  21,  -8,
    -23,  -9,  12,  10,  19,  17,  25, -16,
    -29, -53, -12,  -3,  -1,  18, -14, -19,
   -105, -21, -58, -33, -17, -28, -19, -23,
};
constexpr int KnightEg[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64,
};
constexpr int BishopMg[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21,
};
constexpr int BishopEg[64] = {
    -14, -21, -11,  -8,  -7,  -9, -17, -24,
     -8,  -4,   7, -12,  -3, -13,  -4, -14,
      2,  -8,   0,  -1,  -2,   6,   0,   4,
     -3,   9,  12,   9,  14,  10,   3,   2,
     -6,   3,  13,  19,   7,  10,  -3,  -9,
    -12,  -3,   8,  10,  13,   3,  -7, -15,
    -14, -18,  -7,  -1,   4,  -9, -15, -27,
    -23,  -9, -23,  -5,  -9, -16,  -5, -17,
};
constexpr int RookMg[64] = {
     32,  42,  32,  51,  63,   9,  31,  43,
     27,  32,  58,  62,  80,  67,  26,  44,
     -5,  19,  26,  36,  17,  45,  61,  16,
    -24, -11,   7,  26,  24,  35,  -8, -20,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -44, -16, -20,  -9,  -1,  11,  -6, -71,
    -19, -13,   1,  17,  16,   7, -37, -26,
};
constexpr int RookEg[64] = {
     13,  10,  18,  15,  12,  12,   8,   5,
     11,  13,  13,  11,  -3,   3,   8,   3,
      7,   7,   7,   5,   4,  -3,  -5,  -3,
      4,   3,  13,   1,   2,   1,  -1,   2,
      3,   5,   8,   4,  -5,  -6,  -8, -11,
     -4,   0,  -5,  -1,  -7, -12,  -8, -16,
     -6,  -6,   0,   2,  -9,  -9, -11,  -3,
     -9,   2,   3,  -1,  -5, -13,   4, -20,
};
constexpr int QueenMg[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50,
};
constexpr int QueenEg[64] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41,
};
constexpr int KingMg[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14,
};
constexpr int KingEg[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43,
};

const int* const MgTable[7] = { nullptr, PawnMg, KnightMg, BishopMg, RookMg, QueenMg, KingMg };
const int* const EgTable[7] = { nullptr, PawnEg, KnightEg, BishopEg, RookEg, QueenEg, KingEg };

inline Score psqt(Piece pc, Square s) {
    PieceType pt = type_of(pc);
    int idx = (color_of(pc) == WHITE) ? int(s) ^ 56 : int(s);
    return { MgValue[pt] + MgTable[pt][idx], EgValue[pt] + EgTable[pt][idx] };
}

constexpr Score MobilityBonus[7][28] = {
    {}, {},

    { {-62,-81},{-53,-56},{-12,-30},{-4,-14},{3,8},{13,15},{22,23},{28,27},{33,33} },

    { {-48,-59},{-20,-23},{16,-3},{26,13},{38,24},{51,42},{55,54},{63,57},{63,65},
      {68,73},{81,78},{81,86},{91,88},{98,97} },

    { {-58,-76},{-27,-18},{-15,28},{-10,55},{-5,69},{-2,82},{9,112},{16,118},{30,132},
      {29,142},{32,155},{38,165},{46,166},{48,169},{58,171} },

    { {-39,-36},{-21,-15},{3,8},{3,18},{14,34},{22,54},{28,61},{41,73},{43,79},
      {48,92},{56,94},{60,104},{60,113},{66,120},{67,123},{70,126},{71,133},{73,136},
      {79,140},{88,143},{88,148},{99,166},{102,170},{102,175},{106,184},{109,191},
      {113,206},{116,212} },
    {}
};

constexpr Score PassedRank[8] = {
    {0,0}, {2,38}, {15,36}, {22,50}, {64,81}, {166,184}, {284,269}, {0,0}
};

constexpr Score IsolatedPawn = { -5, -15 };
constexpr Score DoubledPawn  = { -11, -51 };
constexpr Score BackwardPawn = { -9, -24 };
constexpr Score BishopPair   = { 22, 88 };
constexpr Score RookOnOpen   = { 30, 12 };
constexpr Score RookOnSemi   = { 15, 5 };
constexpr Score KnightOutpost= { 30, 21 };
constexpr Score BishopOutpost= { 22, 8 };
constexpr Score ThreatByMinor= { 45, 35 };
constexpr Score ThreatByRook = { 40, 40 };
constexpr Score ThreatByPawn = { 80, 50 };
constexpr Score TempoBonus   = { 21, 12 };

constexpr int KingAttackWeight[7] = { 0, 0, 81, 52, 44, 10, 0 };

struct EvalInfo {
    Bitboard attackedBy[COLOR_NB][7] = {};
    Bitboard attackedBy2[COLOR_NB] = {};
    Bitboard kingRing[COLOR_NB] = {};
    int      kingAttackersCount[COLOR_NB] = {};
    int      kingAttackersWeight[COLOR_NB] = {};
    int      kingAttacksCount[COLOR_NB] = {};
    Bitboard mobilityArea[COLOR_NB] = {};
};

template <Color Us>
void init_side(const Position& pos, EvalInfo& ei) {
    constexpr Color Them = ~Us;
    constexpr Direction Down = pawn_push(Them);

    Square ksq = pos.king_square(Us);
    Bitboard pawns = pos.pieces(Us, PAWN);

    ei.attackedBy[Us][PAWN] = pawn_attacks_bb(Us, pawns);
    ei.attackedBy[Us][KING] = attacks_bb<KING>(ksq);
    ei.attackedBy[Us][ALL_PIECES] = ei.attackedBy[Us][PAWN] | ei.attackedBy[Us][KING];
    ei.attackedBy2[Us] = ei.attackedBy[Us][PAWN] & ei.attackedBy[Us][KING];

    Bitboard lowRanks = (Us == WHITE ? Rank2BB | Rank3BB : Rank7BB | Rank6BB);
    Bitboard blocked = pawns & (shift<Down>(pos.pieces()) | lowRanks);
    ei.mobilityArea[Us] = ~(blocked | pos.pieces(Us, KING, QUEEN)
                            | pawn_attacks_bb(Them, pos.pieces(Them, PAWN)));

    ei.kingRing[Us] = attacks_bb<KING>(ksq) | square_bb(ksq);

    if (file_of(ksq) == FILE_H) ei.kingRing[Us] |= shift<WEST>(ei.kingRing[Us]);
    else if (file_of(ksq) == FILE_A) ei.kingRing[Us] |= shift<EAST>(ei.kingRing[Us]);
    if (rank_of(ksq) == RANK_8) ei.kingRing[Us] |= shift<SOUTH>(ei.kingRing[Us]);
    else if (rank_of(ksq) == RANK_1) ei.kingRing[Us] |= shift<NORTH>(ei.kingRing[Us]);
}

template <Color Us, PieceType Pt>
Score pieces(const Position& pos, EvalInfo& ei) {
    constexpr Color Them = ~Us;
    Score score;

    for (Bitboard b = pos.pieces(Us, Pt); b;) {
        Square s = pop_lsb(b);

        Bitboard occ = pos.pieces();
        if constexpr (Pt == BISHOP || Pt == QUEEN) occ ^= pos.pieces(Us, BISHOP, QUEEN) & ~square_bb(s);
        if constexpr (Pt == ROOK)                  occ ^= pos.pieces(Us, ROOK, QUEEN) & ~square_bb(s);

        Bitboard att = attacks_bb(Pt, s, occ);
        if (pos.blockers_for_king(Us) & square_bb(s))
            att &= LineBB[pos.king_square(Us)][s];

        ei.attackedBy2[Us] |= ei.attackedBy[Us][ALL_PIECES] & att;
        ei.attackedBy[Us][Pt] |= att;
        ei.attackedBy[Us][ALL_PIECES] |= att;

        if (att & ei.kingRing[Them]) {
            ei.kingAttackersCount[Us]++;
            ei.kingAttackersWeight[Us] += KingAttackWeight[Pt];
            ei.kingAttacksCount[Us] += popcount(att & attacks_bb<KING>(pos.king_square(Them)));
        }

        score += MobilityBonus[Pt][popcount(att & ei.mobilityArea[Us])];

        if constexpr (Pt == KNIGHT || Pt == BISHOP) {

            Bitboard outpostRanks = (Us == WHITE ? Rank4BB | Rank5BB | Rank6BB
                                                 : Rank5BB | Rank4BB | Rank3BB);
            if ((outpostRanks & square_bb(s)) && (ei.attackedBy[Us][PAWN] & square_bb(s))
                && !(pos.pieces(Them, PAWN) & pawn_attack_span(Us, s)))
                score += (Pt == KNIGHT ? KnightOutpost : BishopOutpost);
        }

        if constexpr (Pt == ROOK) {
            if (!(pos.pieces(Us, PAWN) & file_bb(s)))
                score += (pos.pieces(Them, PAWN) & file_bb(s)) ? RookOnSemi : RookOnOpen;
        }
    }
    return score;
}

template <Color Us>
Score pawn_structure(const Position& pos) {
    constexpr Color Them = ~Us;
    Bitboard ours = pos.pieces(Us, PAWN), theirs = pos.pieces(Them, PAWN);
    Score score;

    for (Bitboard b = ours; b;) {
        Square s = pop_lsb(b);
        Bitboard neighbours  = ours & adjacent_files_bb(s);
        Bitboard forwardFile = forward_file_bb(Us, s);
        Bitboard support     = neighbours & (rank_bb(s) | rank_bb(s - pawn_push(Us)));

        if (!neighbours) score += IsolatedPawn;
        if (ours & forwardFile) score += DoubledPawn;

        if (!(theirs & passed_pawn_span(Us, s)))
            score += PassedRank[relative_rank(Us, s)];

        else if (!support && (theirs & pawn_attacks_bb(Us, square_bb(s + pawn_push(Us)))))
            score += BackwardPawn;
    }
    return score;
}

template <Color Us>
Score king_safety(const Position& pos, const EvalInfo& ei) {
    constexpr Color Them = ~Us;
    Square ksq = pos.king_square(Us);

    Bitboard weak = ei.attackedBy[Them][ALL_PIECES] & ~ei.attackedBy2[Us]
                  & (~ei.attackedBy[Us][ALL_PIECES] | ei.attackedBy[Us][KING]
                     | ei.attackedBy[Us][QUEEN]);

    int danger = ei.kingAttackersCount[Them] * ei.kingAttackersWeight[Them] / 8
               + 24 * ei.kingAttacksCount[Them]
               + 12 * popcount(ei.kingRing[Us] & weak)
               - 100 * !(pos.pieces(Them, QUEEN))
               - 6 * popcount(pos.pieces(Us, PAWN) & ei.kingRing[Us]);

    Bitboard shelter = pos.pieces(Us, PAWN) & attacks_bb<KING>(ksq);
    danger += 20 * (3 - std::min(3, popcount(shelter)));

    if (danger <= 0) return {};
    return { -danger * danger / 4096, -danger / 16 };
}

template <Color Us>
Score threats(const Position& pos, const EvalInfo& ei) {
    constexpr Color Them = ~Us;
    Score score;

    Bitboard nonPawnEnemies = pos.pieces(Them) & ~pos.pieces(Them, PAWN);
    Bitboard defended = ei.attackedBy[Them][ALL_PIECES];
    Bitboard weak = pos.pieces(Them) & ~defended & ei.attackedBy[Us][ALL_PIECES];

    Bitboard b = (weak | nonPawnEnemies) & (ei.attackedBy[Us][KNIGHT] | ei.attackedBy[Us][BISHOP]);
    score += popcount(b) * ThreatByMinor;

    b = weak & ei.attackedBy[Us][ROOK];
    score += popcount(b) * ThreatByRook;

    b = nonPawnEnemies & ei.attackedBy[Us][PAWN];
    score += popcount(b) * ThreatByPawn;

    return score;
}

Value hce_evaluate(const Position& pos) {
    EvalInfo ei;
    init_side<WHITE>(pos, ei);
    init_side<BLACK>(pos, ei);

    Score score;
    for (Bitboard b = pos.pieces(); b;) {
        Square s = pop_lsb(b);
        Piece pc = pos.piece_on(s);
        Score v = psqt(pc, s);
        if (color_of(pc) == WHITE) score += v; else score -= v;
    }

    score += pieces<WHITE, KNIGHT>(pos, ei) - pieces<BLACK, KNIGHT>(pos, ei);
    score += pieces<WHITE, BISHOP>(pos, ei) - pieces<BLACK, BISHOP>(pos, ei);
    score += pieces<WHITE, ROOK>(pos, ei)   - pieces<BLACK, ROOK>(pos, ei);
    score += pieces<WHITE, QUEEN>(pos, ei)  - pieces<BLACK, QUEEN>(pos, ei);

    score += pawn_structure<WHITE>(pos) - pawn_structure<BLACK>(pos);
    score += king_safety<WHITE>(pos, ei) - king_safety<BLACK>(pos, ei);
    score += threats<WHITE>(pos, ei) - threats<BLACK>(pos, ei);

    if (pos.count(WHITE, BISHOP) >= 2) score += BishopPair;
    if (pos.count(BLACK, BISHOP) >= 2) score -= BishopPair;

    score += (pos.side_to_move() == WHITE ? 1 : -1) * TempoBonus;

    int phase = 1 * pos.count(KNIGHT) + 1 * pos.count(BISHOP)
              + 2 * pos.count(ROOK)   + 4 * pos.count(QUEEN);
    phase = std::min(phase, 24);

    int v = (score.mg * phase + score.eg * (24 - phase)) / 24;

    int pawnCount = pos.count(pos.side_to_move(), PAWN);
    v = v * (100 - 4 * std::max(0, 5 - pawnCount)) / 100;

    v = v * (200 - pos.rule50_count()) / 200;

    v *= EVAL_SCALE;
    return Value(pos.side_to_move() == WHITE ? v : -v);
}

}

Value evaluate(const Position& pos) {
    Value v = nnue::available() ? nnue::evaluate(pos) : hce_evaluate(pos);
    return std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
}

std::string trace(const Position& pos) {
    std::ostringstream os;
    os << "NNUE: ";
    if (nnue::available()) os << nnue::evaluate(pos);
    else os << "n/a";
    os << "\nHCE: " << hce_evaluate(pos)
       << "\nFinal evaluation: " << evaluate(pos) << " internal units = "
       << evaluate(pos) / EVAL_SCALE << " cp (side to move)\n";
    return os.str();
}

}
}
