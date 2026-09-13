#pragma once
#include "types.h"

namespace blitz {

constexpr Bitboard ALL_SQUARES  = ~Bitboard(0);
constexpr Bitboard DARK_SQUARES = 0xAA55AA55AA55AA55ULL;

constexpr Bitboard FileABB = 0x0101010101010101ULL;
constexpr Bitboard FileBBB = FileABB << 1;
constexpr Bitboard FileCBB = FileABB << 2;
constexpr Bitboard FileDBB = FileABB << 3;
constexpr Bitboard FileEBB = FileABB << 4;
constexpr Bitboard FileFBB = FileABB << 5;
constexpr Bitboard FileGBB = FileABB << 6;
constexpr Bitboard FileHBB = FileABB << 7;

constexpr Bitboard Rank1BB = 0xFFULL;
constexpr Bitboard Rank2BB = Rank1BB << (8 * 1);
constexpr Bitboard Rank3BB = Rank1BB << (8 * 2);
constexpr Bitboard Rank4BB = Rank1BB << (8 * 3);
constexpr Bitboard Rank5BB = Rank1BB << (8 * 4);
constexpr Bitboard Rank6BB = Rank1BB << (8 * 5);
constexpr Bitboard Rank7BB = Rank1BB << (8 * 6);
constexpr Bitboard Rank8BB = Rank1BB << (8 * 7);

constexpr Bitboard square_bb(Square s) { return Bitboard(1) << int(s); }
constexpr Bitboard file_bb(File f)     { return FileABB << int(f); }
constexpr Bitboard file_bb(Square s)   { return file_bb(file_of(s)); }
constexpr Bitboard rank_bb(Rank r)     { return Rank1BB << (8 * int(r)); }
constexpr Bitboard rank_bb(Square s)   { return rank_bb(rank_of(s)); }

inline Bitboard  operator&(Bitboard b, Square s) { return b & square_bb(s); }
inline Bitboard  operator|(Bitboard b, Square s) { return b | square_bb(s); }
inline Bitboard  operator^(Bitboard b, Square s) { return b ^ square_bb(s); }
inline Bitboard& operator|=(Bitboard& b, Square s) { return b |= square_bb(s); }
inline Bitboard& operator^=(Bitboard& b, Square s) { return b ^= square_bb(s); }
inline Bitboard  operator|(Square a, Square b) { return square_bb(a) | square_bb(b); }

constexpr bool more_than_one(Bitboard b) { return b & (b - 1); }

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline Square lsb(Bitboard b) { return Square(__builtin_ctzll(b)); }
inline Square msb(Bitboard b) { return Square(63 ^ __builtin_clzll(b)); }
inline Square pop_lsb(Bitboard& b) { Square s = lsb(b); b &= b - 1; return s; }

inline Square frontmost_sq(Color c, Bitboard b) { return c == WHITE ? msb(b) : lsb(b); }

template <Direction D>
constexpr Bitboard shift(Bitboard b) {
    return D == NORTH      ?  b << 8
         : D == SOUTH      ?  b >> 8
         : D == NORTH + NORTH ? b << 16
         : D == SOUTH + SOUTH ? b >> 16
         : D == EAST       ? (b & ~FileHBB) << 1
         : D == WEST       ? (b & ~FileABB) >> 1
         : D == NORTH_EAST ? (b & ~FileHBB) << 9
         : D == NORTH_WEST ? (b & ~FileABB) << 7
         : D == SOUTH_EAST ? (b & ~FileHBB) >> 7
         : D == SOUTH_WEST ? (b & ~FileABB) >> 9
         : 0;
}

template <Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard b) {
    return C == WHITE ? shift<NORTH_WEST>(b) | shift<NORTH_EAST>(b)
                      : shift<SOUTH_WEST>(b) | shift<SOUTH_EAST>(b);
}

constexpr Bitboard pawn_attacks_bb(Color c, Bitboard b) {
    return c == WHITE ? pawn_attacks_bb<WHITE>(b) : pawn_attacks_bb<BLACK>(b);
}

struct Magic {
    Bitboard  mask;
    Bitboard  magic;
    Bitboard* attacks;
    unsigned  shift;

    unsigned index(Bitboard occupied) const {
        return unsigned(((occupied & mask) * magic) >> shift);
    }
    Bitboard operator[](Bitboard occupied) const { return attacks[index(occupied)]; }
};

extern u8       SquareDistance[SQUARE_NB][SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];
extern Bitboard PseudoAttacks[PIECE_TYPE_NB][SQUARE_NB];
extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Magic    RookMagics[SQUARE_NB];
extern Magic    BishopMagics[SQUARE_NB];

void bitboards_init();

inline int distance(Square a, Square b) { return SquareDistance[a][b]; }
inline int file_distance(Square a, Square b) {
    int d = int(file_of(a)) - int(file_of(b));
    return d < 0 ? -d : d;
}
inline int rank_distance(Square a, Square b) {
    int d = int(rank_of(a)) - int(rank_of(b));
    return d < 0 ? -d : d;
}

template <PieceType Pt>
inline Bitboard attacks_bb(Square s, Bitboard occupied) {
    static_assert(Pt == BISHOP || Pt == ROOK || Pt == QUEEN, "unsupported piece type");
    if constexpr (Pt == BISHOP) return BishopMagics[s][occupied];
    else if constexpr (Pt == ROOK) return RookMagics[s][occupied];
    else return BishopMagics[s][occupied] | RookMagics[s][occupied];
}

template <PieceType Pt>
inline Bitboard attacks_bb(Square s) {
    static_assert(Pt != PAWN && Pt != NO_PIECE_TYPE, "pawns are colour dependent");
    return PseudoAttacks[Pt][s];
}

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occupied) {
    switch (pt) {
        case BISHOP: return attacks_bb<BISHOP>(s, occupied);
        case ROOK:   return attacks_bb<ROOK>(s, occupied);
        case QUEEN:  return attacks_bb<QUEEN>(s, occupied);
        default:     return PseudoAttacks[pt][s];
    }
}

inline Bitboard forward_ranks_bb(Color c, Square s) {
    return c == WHITE ? ~Rank1BB << (8 * int(relative_rank(WHITE, s)))
                      : ~Rank8BB >> (8 * int(relative_rank(BLACK, s)));
}

inline Bitboard adjacent_files_bb(Square s) {
    return shift<EAST>(file_bb(s)) | shift<WEST>(file_bb(s));
}

inline Bitboard forward_file_bb(Color c, Square s) {
    return forward_ranks_bb(c, s) & file_bb(s);
}

inline Bitboard pawn_attack_span(Color c, Square s) {
    return forward_ranks_bb(c, s) & adjacent_files_bb(s);
}

inline Bitboard passed_pawn_span(Color c, Square s) {
    return pawn_attack_span(c, s) | forward_file_bb(c, s);
}

inline bool aligned(Square a, Square b, Square c) { return LineBB[a][b] & square_bb(c); }

std::string bitboard_to_string(Bitboard b);

}
