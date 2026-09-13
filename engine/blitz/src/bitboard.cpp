#include "bitboard.h"
#include <algorithm>
#include <sstream>

namespace blitz {

u8       SquareDistance[SQUARE_NB][SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];
Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Magic    RookMagics[SQUARE_NB];
Magic    BishopMagics[SQUARE_NB];

namespace {

Bitboard RookTable[0x19000];
Bitboard BishopTable[0x1480];

class PRNG {
    u64 s_;
public:
    explicit PRNG(u64 seed) : s_(seed) {}
    u64 rand64() {
        s_ ^= s_ >> 12; s_ ^= s_ << 25; s_ ^= s_ >> 27;
        return s_ * 2685821657736338717ULL;
    }

    u64 sparse_rand() { return rand64() & rand64() & rand64(); }
};

Bitboard safe_destination(Square s, int step) {
    Square to = Square(int(s) + step);
    return is_ok(to) && distance(s, to) <= 2 ? square_bb(to) : Bitboard(0);
}

Bitboard sliding_attack(PieceType pt, Square sq, Bitboard occupied) {
    Bitboard attacks = 0;
    const Direction rook_dirs[4]   = { NORTH, SOUTH, EAST, WEST };
    const Direction bishop_dirs[4] = { NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };

    for (Direction d : (pt == ROOK ? rook_dirs : bishop_dirs)) {
        Square s = sq;
        while (safe_destination(s, d) && !(occupied & square_bb(s))) {
            s += d;
            attacks |= square_bb(s);
        }
    }
    return attacks;
}

void init_magics(PieceType pt, Bitboard table[], Magic magics[]) {
    Bitboard occupancy[4096], reference[4096];
    int      epoch[4096] = {}, cnt = 0, size = 0;

    for (Square s = SQ_A1; s <= SQ_H8; s = Square(int(s) + 1)) {

        Bitboard edges = ((Rank1BB | Rank8BB) & ~rank_bb(s)) | ((FileABB | FileHBB) & ~file_bb(s));

        Magic& m = magics[s];
        m.mask   = sliding_attack(pt, s, 0) & ~edges;
        m.shift  = 64 - popcount(m.mask);
        m.attacks = (s == SQ_A1) ? table : magics[int(s) - 1].attacks + size;

        Bitboard b = 0;
        size = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attack(pt, s, b);
            size++;
            b = (b - m.mask) & m.mask;
        } while (b);

        PRNG rng(0x9E3779B97F4A7C15ULL ^ (u64(s) * 0xBF58476D1CE4E5B9ULL));

        for (int i = 0; i < size;) {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6;)
                m.magic = rng.sparse_rand();

            for (++cnt, i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx]      = cnt;
                    m.attacks[idx]  = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    break;
                }
            }
        }
    }
}

}

void bitboards_init() {
    for (Square a = SQ_A1; a <= SQ_H8; a = Square(int(a) + 1))
        for (Square b = SQ_A1; b <= SQ_H8; b = Square(int(b) + 1))
            SquareDistance[a][b] = u8(std::max(file_distance(a, b), rank_distance(a, b)));

    init_magics(ROOK,   RookTable,   RookMagics);
    init_magics(BISHOP, BishopTable, BishopMagics);

    for (Square s = SQ_A1; s <= SQ_H8; s = Square(int(s) + 1)) {
        PawnAttacks[WHITE][s] = pawn_attacks_bb<WHITE>(square_bb(s));
        PawnAttacks[BLACK][s] = pawn_attacks_bb<BLACK>(square_bb(s));

        for (int step : { -17, -15, -10, -6, 6, 10, 15, 17 })
            PseudoAttacks[KNIGHT][s] |= safe_destination(s, step);
        for (int step : { -9, -8, -7, -1, 1, 7, 8, 9 })
            PseudoAttacks[KING][s] |= safe_destination(s, step);

        PseudoAttacks[BISHOP][s] = attacks_bb<BISHOP>(s, 0);
        PseudoAttacks[ROOK][s]   = attacks_bb<ROOK>(s, 0);
        PseudoAttacks[QUEEN][s]  = PseudoAttacks[BISHOP][s] | PseudoAttacks[ROOK][s];

        for (PieceType pt : { BISHOP, ROOK })
            for (Square t = SQ_A1; t <= SQ_H8; t = Square(int(t) + 1)) {
                if (!(PseudoAttacks[pt][s] & square_bb(t))) continue;
                LineBB[s][t]    = (attacks_bb(pt, s, 0) & attacks_bb(pt, t, 0)) | s | t;
                BetweenBB[s][t] = attacks_bb(pt, s, square_bb(t)) & attacks_bb(pt, t, square_bb(s));
            }

        for (Square t = SQ_A1; t <= SQ_H8; t = Square(int(t) + 1))
            BetweenBB[s][t] |= square_bb(t);
    }
}

std::string bitboard_to_string(Bitboard b) {
    std::ostringstream os;
    os << "+---+---+---+---+---+---+---+---+\n";
    for (Rank r = RANK_8; r >= RANK_1; r = Rank(int(r) - 1)) {
        for (File f = FILE_A; f <= FILE_H; f = File(int(f) + 1))
            os << "| " << ((b & square_bb(make_square(f, r))) ? 'X' : ' ') << ' ';
        os << "| " << (1 + int(r)) << "\n+---+---+---+---+---+---+---+---+\n";
    }
    os << "  a   b   c   d   e   f   g   h\n";
    return os.str();
}

std::string square_to_string(Square s) {
    if (s == SQ_NONE) return "-";
    return std::string{ char('a' + int(file_of(s))), char('1' + int(rank_of(s))) };
}

std::string move_to_uci(Move m, bool chess960) {
    if (m == Move::none()) return "(none)";
    if (m == Move::null()) return "0000";

    Square from = m.from(), to = m.to();

    if (m.type() == CASTLING && !chess960)
        to = make_square(to > from ? FILE_G : FILE_C, rank_of(from));

    std::string s = square_to_string(from) + square_to_string(to);
    if (m.type() == PROMOTION) s += " pnbrqk"[int(m.promotion())];
    return s;
}

}
