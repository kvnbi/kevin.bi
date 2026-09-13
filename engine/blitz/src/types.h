#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

namespace blitz {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using Bitboard = u64;
using Key      = u64;

enum Color : int { WHITE = 0, BLACK = 1, COLOR_NB = 2 };

constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int {
    NO_PIECE_TYPE = 0, PAWN = 1, KNIGHT, BISHOP, ROOK, QUEEN, KING,
    ALL_PIECES = 0,
    PIECE_TYPE_NB = 8
};

enum Piece : int {
    NO_PIECE = 0,
    W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    PIECE_NB = 16
};

constexpr Piece     make_piece(Color c, PieceType pt) { return Piece((int(c) << 3) + int(pt)); }
constexpr PieceType type_of(Piece p)  { return PieceType(int(p) & 7); }
constexpr Color     color_of(Piece p) { return Color(int(p) >> 3); }

enum Square : int {
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64, SQUARE_NB = 64
};

enum File : int { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, FILE_NB };
enum Rank : int { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, RANK_NB };

constexpr File   file_of(Square s) { return File(int(s) & 7); }
constexpr Rank   rank_of(Square s) { return Rank(int(s) >> 3); }
constexpr Square make_square(File f, Rank r) { return Square((int(r) << 3) | int(f)); }
constexpr Square flip_rank(Square s) { return Square(int(s) ^ 56); }
constexpr Square flip_file(Square s) { return Square(int(s) ^ 7); }

constexpr Square relative_square(Color c, Square s) { return Square(int(s) ^ (int(c) * 56)); }
constexpr Rank   relative_rank(Color c, Rank r)     { return Rank(int(r) ^ (int(c) * 7)); }
constexpr Rank   relative_rank(Color c, Square s)   { return relative_rank(c, rank_of(s)); }

constexpr bool is_ok(Square s) { return s >= SQ_A1 && s <= SQ_H8; }

enum Direction : int {
    NORTH =  8, EAST =  1, SOUTH = -8, WEST = -1,
    NORTH_EAST = NORTH + EAST, SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST, NORTH_WEST = NORTH + WEST
};

constexpr Square operator+(Square s, Direction d) { return Square(int(s) + int(d)); }
constexpr Square operator-(Square s, Direction d) { return Square(int(s) - int(d)); }
constexpr Square& operator+=(Square& s, Direction d) { return s = s + d; }
constexpr Square& operator-=(Square& s, Direction d) { return s = s - d; }
constexpr Direction operator-(Direction d) { return Direction(-int(d)); }
constexpr Direction pawn_push(Color c) { return c == WHITE ? NORTH : SOUTH; }

enum CastlingRights : int {
    NO_CASTLING = 0,
    WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8,
    KING_SIDE  = WHITE_OO  | BLACK_OO,
    QUEEN_SIDE = WHITE_OOO | BLACK_OOO,
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ANY_CASTLING = WHITE_CASTLING | BLACK_CASTLING,
    CASTLING_RIGHT_NB = 16
};

constexpr CastlingRights castling_rights(Color c) {
    return c == WHITE ? WHITE_CASTLING : BLACK_CASTLING;
}

enum MoveType : int {
    NORMAL     = 0,
    PROMOTION  = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING   = 3 << 14
};

class Move {
    u16 data_;
public:
    Move() = default;
    constexpr explicit Move(u16 d) : data_(d) {}
    constexpr Move(Square from, Square to) : data_(u16(int(from) | (int(to) << 6))) {}

    template <MoveType T>
    static constexpr Move make(Square from, Square to, PieceType promo = KNIGHT) {
        return Move(u16(T | ((int(promo) - int(KNIGHT)) << 12) | int(from) | (int(to) << 6)));
    }

    constexpr Square    from()      const { return Square(data_ & 0x3F); }
    constexpr Square    to()        const { return Square((data_ >> 6) & 0x3F); }
    constexpr int       from_to()   const { return data_ & 0xFFF; }
    constexpr MoveType  type()      const { return MoveType(data_ & (3 << 14)); }
    constexpr PieceType promotion() const { return PieceType(((data_ >> 12) & 3) + int(KNIGHT)); }
    constexpr u16       raw()       const { return data_; }

    constexpr bool operator==(const Move& m) const { return data_ == m.data_; }
    constexpr bool operator!=(const Move& m) const { return data_ != m.data_; }
    constexpr explicit operator bool() const { return data_ != 0; }

    static constexpr Move none() { return Move(0); }
    static constexpr Move null() { return Move(65); }
};

using Value = int;

constexpr Value VALUE_ZERO      = 0;
constexpr Value VALUE_DRAW      = 0;
constexpr Value VALUE_NONE      = 32002;
constexpr Value VALUE_INFINITE  = 32001;
constexpr Value VALUE_MATE      = 32000;
constexpr Value MAX_PLY         = 246;
constexpr Value VALUE_MATE_IN_MAX_PLY  =  VALUE_MATE - MAX_PLY;
constexpr Value VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY;
constexpr Value VALUE_TB_WIN_IN_MAX_PLY = VALUE_MATE_IN_MAX_PLY - 1;

constexpr Value mate_in(int ply)  { return  VALUE_MATE - ply; }
constexpr Value mated_in(int ply) { return -VALUE_MATE + ply; }

constexpr int EVAL_SCALE = 2;
constexpr Value PieceValue[PIECE_TYPE_NB] = { 0, 208, 665, 686, 1040, 1976, 0, 0 };

constexpr int MAX_MOVES = 256;

enum Bound : u8 { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

std::string square_to_string(Square s);
std::string move_to_uci(Move m, bool chess960 = false);

}
