#pragma once
#include "bitboard.h"
#include "nnue/arch.h"
#include <string>
#include <iosfwd>

namespace blitz {

class Position;

struct StateInfo {

    Key    pawnKey;
    Key    materialKey;
    Value  nonPawnMaterial[COLOR_NB];
    int    castlingRights;
    int    rule50;
    int    pliesFromNull;
    Square epSquare;

    Key      key;
    Bitboard checkersBB;
    StateInfo* previous;
    Bitboard blockersForKing[COLOR_NB];
    Bitboard pinners[COLOR_NB];
    Bitboard checkSquares[PIECE_TYPE_NB];
    Piece    capturedPiece;
    int      repetition;

    nnue::Accumulator accumulator;
    nnue::DirtyPiece  dirtyPiece;
};

class Position {
public:
    static void init();

    Position() = default;
    Position(const Position&) = delete;
    Position& operator=(const Position&) = delete;

    Position& set(const std::string& fen, bool isChess960, StateInfo* si);
    Position& set(const Position& other, StateInfo* si);
    std::string fen() const;

    Bitboard pieces() const { return occupied_; }
    Bitboard pieces(PieceType pt) const { return byType_[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType_[a] | byType_[b]; }
    Bitboard pieces(Color c) const { return byColor_[c]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor_[c] & byType_[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const { return byColor_[c] & (byType_[a] | byType_[b]); }

    Piece piece_on(Square s) const { return board_[s]; }
    bool  empty(Square s) const { return board_[s] == NO_PIECE; }
    Square king_square(Color c) const { return lsb(pieces(c, KING)); }
    int   count(Color c, PieceType pt) const { return pieceCount_[make_piece(c, pt)]; }
    int   count(PieceType pt) const { return pieceCount_[make_piece(WHITE, pt)] + pieceCount_[make_piece(BLACK, pt)]; }
    int   count_all() const { return popcount(occupied_); }

    Color side_to_move() const { return sideToMove_; }
    int   game_ply() const { return gamePly_; }
    bool  is_chess960() const { return chess960_; }

    Key      key() const { return st_->key; }
    Key      pawn_key() const { return st_->pawnKey; }
    Key      material_key() const { return st_->materialKey; }
    Square   ep_square() const { return st_->epSquare; }
    int      rule50_count() const { return st_->rule50; }
    Bitboard checkers() const { return st_->checkersBB; }
    Bitboard blockers_for_king(Color c) const { return st_->blockersForKing[c]; }
    Bitboard pinners(Color c) const { return st_->pinners[c]; }
    Bitboard check_squares(PieceType pt) const { return st_->checkSquares[pt]; }
    Value    non_pawn_material(Color c) const { return st_->nonPawnMaterial[c]; }
    Value    non_pawn_material() const { return st_->nonPawnMaterial[WHITE] + st_->nonPawnMaterial[BLACK]; }
    StateInfo* state() const { return st_; }

    int  castling_rights() const { return st_->castlingRights; }
    bool can_castle(CastlingRights cr) const { return st_->castlingRights & cr; }
    bool castling_impeded(CastlingRights cr) const { return occupied_ & castlingPath_[cr]; }
    Square castling_rook_square(CastlingRights cr) const { return castlingRookSquare_[cr]; }

    Bitboard attackers_to(Square s) const { return attackers_to(s, occupied_); }
    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attacks_by(Color c, PieceType pt) const;

    Bitboard slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const;

    bool legal(Move m) const;
    bool pseudo_legal(Move m) const;
    bool capture(Move m) const;
    bool capture_stage(Move m) const;
    bool gives_check(Move m) const;
    Piece moved_piece(Move m) const { return board_[m.from()]; }
    Piece captured_piece() const { return st_->capturedPiece; }
    bool  advanced_pawn_push(Move m) const;

    void do_move(Move m, StateInfo& newSt);
    void do_move(Move m, StateInfo& newSt, bool givesCheck);
    void undo_move(Move m);
    void do_null_move(StateInfo& newSt);
    void undo_null_move();

    bool see_ge(Move m, Value threshold = VALUE_ZERO) const;

    bool is_draw(int ply) const;
    bool has_repeated() const;
    bool upcoming_repetition(int ply) const;
    bool has_non_pawn_material(Color c) const { return st_->nonPawnMaterial[c] > VALUE_ZERO; }

    Key key_after(Move m) const;

    bool pos_is_ok() const;
    std::string to_string() const;

private:
    void set_castling_right(Color c, Square rfrom);
    void set_state() const;
    void set_check_info() const;

    void put_piece(Piece pc, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    template <bool Do> void do_castling(Color us, Square from, Square& to, Square& rfrom, Square& rto);

    Bitboard byType_[PIECE_TYPE_NB] = {};
    Bitboard byColor_[COLOR_NB] = {};
    Bitboard occupied_ = 0;
    Piece    board_[SQUARE_NB];
    u8       pieceCount_[PIECE_NB] = {};
    int      castlingRightsMask_[SQUARE_NB] = {};
    Square   castlingRookSquare_[CASTLING_RIGHT_NB];
    Bitboard castlingPath_[CASTLING_RIGHT_NB] = {};
    int      gamePly_ = 0;
    Color    sideToMove_ = WHITE;
    bool     chess960_ = false;
    StateInfo* st_ = nullptr;
};

std::ostream& operator<<(std::ostream& os, const Position& pos);

inline bool Position::capture(Move m) const {
    return (!empty(m.to()) && m.type() != CASTLING) || m.type() == EN_PASSANT;
}

inline bool Position::capture_stage(Move m) const {
    return capture(m) || (m.type() == PROMOTION && m.promotion() == QUEEN);
}

inline bool Position::advanced_pawn_push(Move m) const {
    return type_of(moved_piece(m)) == PAWN && relative_rank(sideToMove_, m.to()) > RANK_5;
}

inline void Position::do_move(Move m, StateInfo& newSt) { do_move(m, newSt, gives_check(m)); }

inline void Position::put_piece(Piece pc, Square s) {
    board_[s] = pc;
    byType_[type_of(pc)] |= s;
    byColor_[color_of(pc)] |= s;
    occupied_ |= s;
    pieceCount_[pc]++;
}

inline void Position::remove_piece(Square s) {
    Piece pc = board_[s];
    byType_[type_of(pc)] ^= s;
    byColor_[color_of(pc)] ^= s;
    occupied_ ^= s;
    board_[s] = NO_PIECE;
    pieceCount_[pc]--;
}

inline void Position::move_piece(Square from, Square to) {
    Piece pc = board_[from];
    Bitboard fromTo = square_bb(from) | square_bb(to);
    byType_[type_of(pc)] ^= fromTo;
    byColor_[color_of(pc)] ^= fromTo;
    occupied_ ^= fromTo;
    board_[from] = NO_PIECE;
    board_[to] = pc;
}

}
