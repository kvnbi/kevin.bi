#include "position.h"
#include "movegen.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace blitz {

namespace Zobrist {
    Key psq[PIECE_NB][SQUARE_NB];
    Key enpassant[FILE_NB];
    Key castling[CASTLING_RIGHT_NB];
    Key side;
}

namespace {

constexpr std::string_view PieceToChar = " PNBRQK  pnbrqk";

Key  cuckoo[8192];
Move cuckooMove[8192];

inline int H1(Key h) { return int(h & 0x1FFF); }
inline int H2(Key h) { return int((h >> 16) & 0x1FFF); }

class PRNG {
    u64 s_;
public:
    explicit PRNG(u64 seed) : s_(seed) {}
    u64 rand64() {
        s_ ^= s_ >> 12; s_ ^= s_ << 25; s_ ^= s_ >> 27;
        return s_ * 2685821657736338717ULL;
    }
};

}

void Position::init() {
    PRNG rng(1070372);

    for (Piece pc = NO_PIECE; pc < PIECE_NB; pc = Piece(int(pc) + 1))
        for (Square s = SQ_A1; s <= SQ_H8; s = Square(int(s) + 1))
            Zobrist::psq[pc][s] = rng.rand64();

    for (File f = FILE_A; f <= FILE_H; f = File(int(f) + 1))
        Zobrist::enpassant[f] = rng.rand64();

    for (int cr = 0; cr < CASTLING_RIGHT_NB; ++cr)
        Zobrist::castling[cr] = rng.rand64();

    Zobrist::side = rng.rand64();

    std::memset(cuckoo, 0, sizeof(cuckoo));
    std::fill(std::begin(cuckooMove), std::end(cuckooMove), Move::none());

    int count = 0;
    for (Piece pc : { W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
                      B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING }) {
        for (Square s1 = SQ_A1; s1 <= SQ_H8; s1 = Square(int(s1) + 1))
            for (Square s2 = Square(int(s1) + 1); s2 <= SQ_H8; s2 = Square(int(s2) + 1)) {
                if (!(attacks_bb(type_of(pc), s1, 0) & square_bb(s2))) continue;

                Move move = Move(s1, s2);
                Key  key  = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                int  i    = H1(key);

                while (true) {
                    std::swap(cuckoo[i], key);
                    std::swap(cuckooMove[i], move);
                    if (move == Move::none()) break;
                    i = (i == H1(key)) ? H2(key) : H1(key);
                }
                count++;
            }
    }
    assert(count == 3668);
    (void)count;
}

Position& Position::set(const std::string& fenStr, bool isChess960, StateInfo* si) {
    unsigned char col, row, token;
    size_t idx;
    Square sq = SQ_A8;
    std::istringstream ss(fenStr);

    std::memset(this, 0, sizeof(Position));
    std::memset(si, 0, sizeof(StateInfo));
    std::fill(std::begin(board_), std::end(board_), NO_PIECE);
    std::fill(std::begin(castlingRookSquare_), std::end(castlingRookSquare_), SQ_NONE);
    st_ = si;

    ss >> std::noskipws;

    while ((ss >> token) && !isspace(token)) {
        if (isdigit(token))
            sq += Direction(token - '0');
        else if (token == '/')
            sq = Square(int(sq) - 16);
        else if ((idx = PieceToChar.find(token)) != std::string::npos) {
            put_piece(Piece(idx), sq);
            sq += EAST;
        }
    }

    ss >> token;
    sideToMove_ = (token == 'w' ? WHITE : BLACK);
    ss >> token;

    while ((ss >> token) && !isspace(token)) {
        Square rsq;
        Color  c = islower(token) ? BLACK : WHITE;
        Piece  rook = make_piece(c, ROOK);
        token = char(toupper(token));

        if (token == 'K')
            for (rsq = relative_square(c, SQ_H1); board_[rsq] != rook; rsq += WEST) {}
        else if (token == 'Q')
            for (rsq = relative_square(c, SQ_A1); board_[rsq] != rook; rsq += EAST) {}
        else if (token >= 'A' && token <= 'H')
            rsq = make_square(File(token - 'A'), relative_rank(c, RANK_1));
        else
            continue;

        set_castling_right(c, rsq);
    }

    bool enpassant = false;
    if (((ss >> col) && (col >= 'a' && col <= 'h')) && ((ss >> row) && (row == (sideToMove_ == WHITE ? '6' : '3')))) {
        st_->epSquare = make_square(File(col - 'a'), Rank(row - '1'));

        enpassant = (attackers_to(st_->epSquare) & pieces(sideToMove_, PAWN))
                 && (pieces(~sideToMove_, PAWN) & square_bb(st_->epSquare + pawn_push(~sideToMove_)))
                 && !(occupied_ & (square_bb(st_->epSquare) | square_bb(st_->epSquare + pawn_push(sideToMove_))));
    }
    if (!enpassant) st_->epSquare = SQ_NONE;

    int fullmove = 1;
    ss >> std::skipws >> st_->rule50 >> fullmove;
    gamePly_ = std::max(2 * (fullmove - 1), 0) + (sideToMove_ == BLACK);

    chess960_ = isChess960;
    set_state();
    assert(pos_is_ok());
    return *this;
}

Position& Position::set(const Position& other, StateInfo* si) {
    return set(other.fen(), other.chess960_, si);
}

void Position::set_castling_right(Color c, Square rfrom) {
    Square kfrom = king_square(c);
    CastlingRights cr = CastlingRights((kfrom < rfrom ? WHITE_OO : WHITE_OOO) << (2 * int(c)));

    st_->castlingRights |= cr;
    castlingRightsMask_[kfrom] |= cr;
    castlingRightsMask_[rfrom] |= cr;
    castlingRookSquare_[cr] = rfrom;

    Square kto = relative_square(c, (cr & KING_SIDE) ? SQ_G1 : SQ_C1);
    Square rto = relative_square(c, (cr & KING_SIDE) ? SQ_F1 : SQ_D1);

    castlingPath_[cr] = (BetweenBB[rfrom][rto] | BetweenBB[kfrom][kto])
                      & ~(square_bb(kfrom) | square_bb(rfrom));
}

void Position::set_check_info() const {
    st_->blockersForKing[WHITE] = slider_blockers(pieces(BLACK), king_square(WHITE), st_->pinners[BLACK]);
    st_->blockersForKing[BLACK] = slider_blockers(pieces(WHITE), king_square(BLACK), st_->pinners[WHITE]);

    Square ksq = king_square(~sideToMove_);
    st_->checkSquares[PAWN]   = PawnAttacks[~sideToMove_][ksq];
    st_->checkSquares[KNIGHT] = attacks_bb<KNIGHT>(ksq);
    st_->checkSquares[BISHOP] = attacks_bb<BISHOP>(ksq, occupied_);
    st_->checkSquares[ROOK]   = attacks_bb<ROOK>(ksq, occupied_);
    st_->checkSquares[QUEEN]  = st_->checkSquares[BISHOP] | st_->checkSquares[ROOK];
    st_->checkSquares[KING]   = 0;
}

void Position::set_state() const {
    st_->key = st_->materialKey = 0;
    st_->pawnKey = Zobrist::psq[NO_PIECE][0];
    st_->nonPawnMaterial[WHITE] = st_->nonPawnMaterial[BLACK] = VALUE_ZERO;
    st_->checkersBB = attackers_to(king_square(sideToMove_)) & pieces(~sideToMove_);

    set_check_info();

    for (Bitboard b = occupied_; b;) {
        Square s  = pop_lsb(b);
        Piece  pc = piece_on(s);
        st_->key ^= Zobrist::psq[pc][s];

        if (type_of(pc) == PAWN)
            st_->pawnKey ^= Zobrist::psq[pc][s];
        else if (type_of(pc) != KING)
            st_->nonPawnMaterial[color_of(pc)] += PieceValue[type_of(pc)];
    }

    if (st_->epSquare != SQ_NONE) st_->key ^= Zobrist::enpassant[file_of(st_->epSquare)];
    if (sideToMove_ == BLACK)     st_->key ^= Zobrist::side;
    st_->key ^= Zobrist::castling[st_->castlingRights];

    for (Piece pc : { W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
                      B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING })
        for (int cnt = 0; cnt < pieceCount_[pc]; ++cnt)
            st_->materialKey ^= Zobrist::psq[pc][cnt];
}

std::string Position::fen() const {
    std::ostringstream ss;

    for (Rank r = RANK_8; r >= RANK_1; r = Rank(int(r) - 1)) {
        for (File f = FILE_A; f <= FILE_H; f = File(int(f) + 1)) {
            int emptyCnt = 0;
            while (f <= FILE_H && empty(make_square(f, r))) { emptyCnt++; f = File(int(f) + 1); }
            if (emptyCnt) ss << emptyCnt;
            if (f <= FILE_H) ss << PieceToChar[piece_on(make_square(f, r))];
        }
        if (r > RANK_1) ss << '/';
    }

    ss << (sideToMove_ == WHITE ? " w " : " b ");

    auto rook_file = [&](CastlingRights cr) { return char('A' + int(file_of(castling_rook_square(cr)))); };
    if (can_castle(WHITE_OO))  ss << (chess960_ ? rook_file(WHITE_OO) : 'K');
    if (can_castle(WHITE_OOO)) ss << (chess960_ ? rook_file(WHITE_OOO) : 'Q');
    if (can_castle(BLACK_OO))  ss << char(chess960_ ? tolower(rook_file(BLACK_OO)) : 'k');
    if (can_castle(BLACK_OOO)) ss << char(chess960_ ? tolower(rook_file(BLACK_OOO)) : 'q');
    if (!can_castle(ANY_CASTLING)) ss << '-';

    ss << ' ' << square_to_string(st_->epSquare)
       << ' ' << st_->rule50
       << ' ' << 1 + (gamePly_ - (sideToMove_ == BLACK)) / 2;

    return ss.str();
}

Bitboard Position::slider_blockers(Bitboard sliders, Square s, Bitboard& pinners) const {
    Bitboard blockers = 0;
    pinners = 0;

    Bitboard snipers = ((attacks_bb<ROOK>(s) & pieces(ROOK, QUEEN))
                      | (attacks_bb<BISHOP>(s) & pieces(BISHOP, QUEEN))) & sliders;
    Bitboard occupancy = occupied_ ^ snipers;

    while (snipers) {
        Square sniperSq = pop_lsb(snipers);
        Bitboard b = BetweenBB[s][sniperSq] & ~square_bb(sniperSq) & occupancy;

        if (b && !more_than_one(b)) {
            blockers |= b;
            if (b & pieces(color_of(piece_on(s)))) pinners |= sniperSq;
        }
    }
    return blockers;
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN))
         | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN))
         | (attacks_bb<KNIGHT>(s) & pieces(KNIGHT))
         | (attacks_bb<ROOK>(s, occ) & pieces(ROOK, QUEEN))
         | (attacks_bb<BISHOP>(s, occ) & pieces(BISHOP, QUEEN))
         | (attacks_bb<KING>(s) & pieces(KING));
}

Bitboard Position::attacks_by(Color c, PieceType pt) const {
    if (pt == PAWN) return pawn_attacks_bb(c, pieces(c, PAWN));

    Bitboard threats = 0;
    for (Bitboard b = pieces(c, pt); b;)
        threats |= attacks_bb(pt, pop_lsb(b), occupied_);
    return threats;
}

bool Position::legal(Move m) const {
    Color us = sideToMove_;
    Square from = m.from(), to = m.to();

    if (m.type() == EN_PASSANT) {

        Square ksq = king_square(us);
        Square capsq = to - pawn_push(us);
        Bitboard occ = (occupied_ ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);

        return !(attacks_bb<ROOK>(ksq, occ) & pieces(~us, ROOK, QUEEN))
            && !(attacks_bb<BISHOP>(ksq, occ) & pieces(~us, BISHOP, QUEEN));
    }

    if (m.type() == CASTLING) {

        Square kto = relative_square(us, to > from ? SQ_G1 : SQ_C1);
        Direction step = kto > from ? EAST : WEST;

        for (Square s = kto; s != from; s -= step)
            if (attackers_to(s) & pieces(~us)) return false;

        return !chess960_ || !(blockers_for_king(us) & square_bb(m.to()));
    }

    if (type_of(piece_on(from)) == KING)
        return !(attackers_to(to, occupied_ ^ square_bb(from)) & pieces(~us));

    return !(blockers_for_king(us) & square_bb(from)) || aligned(from, to, king_square(us));
}

bool Position::pseudo_legal(Move m) const {
    Color us = sideToMove_;
    Square from = m.from(), to = m.to();
    Piece pc = moved_piece(m);

    if (m.type() != NORMAL)
        return MoveList<LEGAL>(*this).contains(m);

    if (m.promotion() != KNIGHT) return false;
    if (pc == NO_PIECE || color_of(pc) != us) return false;
    if (pieces(us) & square_bb(to)) return false;

    if (type_of(pc) == PAWN) {
        if ((Rank8BB | Rank1BB) & square_bb(to)) return false;

        if (!(PawnAttacks[us][from] & pieces(~us) & square_bb(to))
            && !((from + pawn_push(us) == to) && empty(to))
            && !((from + 2 * pawn_push(us) == to) && relative_rank(us, from) == RANK_2
                 && empty(to) && empty(to - pawn_push(us))))
            return false;
    } else if (!(attacks_bb(type_of(pc), from, occupied_) & square_bb(to))) {
        return false;
    }

    if (checkers()) {
        if (type_of(pc) != KING) {
            if (more_than_one(checkers())) return false;
            if (!(BetweenBB[king_square(us)][lsb(checkers())] & square_bb(to))) return false;
        } else if (attackers_to(to, occupied_ ^ square_bb(from)) & pieces(~us)) {
            return false;
        }
    }
    return true;
}

bool Position::gives_check(Move m) const {
    Square from = m.from(), to = m.to();

    if (check_squares(type_of(piece_on(from))) & square_bb(to)) return true;

    if ((blockers_for_king(~sideToMove_) & square_bb(from))
        && !aligned(from, to, king_square(~sideToMove_)))
        return true;

    switch (m.type()) {
        case NORMAL:
            return false;

        case PROMOTION:
            return attacks_bb(m.promotion(), to, occupied_ ^ square_bb(from))
                 & square_bb(king_square(~sideToMove_));

        case EN_PASSANT: {

            Square capsq = make_square(file_of(to), rank_of(from));
            Bitboard b = (occupied_ ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
            Square ksq = king_square(~sideToMove_);
            return (attacks_bb<ROOK>(ksq, b) & pieces(sideToMove_, ROOK, QUEEN))
                 | (attacks_bb<BISHOP>(ksq, b) & pieces(sideToMove_, BISHOP, QUEEN));
        }

        default: {
            Square rto = relative_square(sideToMove_, to > from ? SQ_F1 : SQ_D1);
            Square ksq = king_square(~sideToMove_);
            return (attacks_bb<ROOK>(rto) & square_bb(ksq))
                && (attacks_bb<ROOK>(rto, occupied_ ^ square_bb(from) ^ square_bb(to)) & square_bb(ksq));
        }
    }
}

void Position::do_move(Move m, StateInfo& newSt, bool givesCheck) {
    assert(m != Move::none() && m != Move::null());

    Key k = st_->key ^ Zobrist::side;

    std::memcpy(&newSt, st_, offsetof(StateInfo, key));
    newSt.previous = st_;
    st_ = &newSt;

    st_->rule50++;
    st_->pliesFromNull++;
    st_->accumulator.computed[WHITE] = st_->accumulator.computed[BLACK] = false;

    auto& dp = st_->dirtyPiece;
    dp.dirty_num = 1;

    Color us = sideToMove_, them = ~us;
    Square from = m.from(), to = m.to();
    Piece pc = piece_on(from);
    Piece captured = (m.type() == EN_PASSANT) ? make_piece(them, PAWN) : piece_on(to);

    if (m.type() == CASTLING) {
        Square rfrom, rto;
        do_castling<true>(us, from, to, rfrom, rto);
        k ^= Zobrist::psq[captured][rfrom] ^ Zobrist::psq[captured][rto];
        captured = NO_PIECE;
    }

    if (captured) {
        Square capsq = to;

        if (type_of(captured) == PAWN) {
            if (m.type() == EN_PASSANT) capsq -= pawn_push(us);
            st_->pawnKey ^= Zobrist::psq[captured][capsq];
        } else {
            st_->nonPawnMaterial[them] -= PieceValue[type_of(captured)];
        }

        dp.dirty_num = 2;
        dp.piece[1] = captured;
        dp.from[1]  = capsq;
        dp.to[1]    = SQ_NONE;

        remove_piece(capsq);
        k ^= Zobrist::psq[captured][capsq];
        st_->materialKey ^= Zobrist::psq[captured][pieceCount_[captured]];
        st_->rule50 = 0;
    }

    k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];

    if (st_->epSquare != SQ_NONE) {
        k ^= Zobrist::enpassant[file_of(st_->epSquare)];
        st_->epSquare = SQ_NONE;
    }

    if (st_->castlingRights && (castlingRightsMask_[from] | castlingRightsMask_[to])) {
        k ^= Zobrist::castling[st_->castlingRights];
        st_->castlingRights &= ~(castlingRightsMask_[from] | castlingRightsMask_[to]);
        k ^= Zobrist::castling[st_->castlingRights];
    }

    if (m.type() != CASTLING) {
        dp.piece[0] = pc;
        dp.from[0]  = from;
        dp.to[0]    = to;
        move_piece(from, to);
    }

    if (type_of(pc) == PAWN) {

        if ((int(to) ^ int(from)) == 16
            && (pawn_attacks_bb(us, square_bb(to - pawn_push(us))) & pieces(them, PAWN))) {
            st_->epSquare = to - pawn_push(us);
            k ^= Zobrist::enpassant[file_of(st_->epSquare)];
        } else if (m.type() == PROMOTION) {
            Piece promo = make_piece(us, m.promotion());

            remove_piece(to);
            put_piece(promo, to);

            dp.to[0] = SQ_NONE;
            dp.piece[dp.dirty_num] = promo;
            dp.from[dp.dirty_num] = SQ_NONE;
            dp.to[dp.dirty_num] = to;
            dp.dirty_num++;

            k ^= Zobrist::psq[pc][to] ^ Zobrist::psq[promo][to];
            st_->pawnKey ^= Zobrist::psq[pc][to];
            st_->materialKey ^= Zobrist::psq[promo][pieceCount_[promo] - 1]
                              ^ Zobrist::psq[pc][pieceCount_[pc]];
            st_->nonPawnMaterial[us] += PieceValue[m.promotion()];
        }

        st_->pawnKey ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
        st_->rule50 = 0;
    }

    st_->capturedPiece = captured;
    st_->key = k;
    st_->checkersBB = givesCheck ? (attackers_to(king_square(them)) & pieces(us)) : Bitboard(0);

    sideToMove_ = them;
    gamePly_++;
    set_check_info();

    st_->repetition = 0;
    int end = std::min(st_->rule50, st_->pliesFromNull);
    if (end >= 4) {
        StateInfo* stp = st_->previous->previous;
        for (int i = 4; i <= end; i += 2) {
            stp = stp->previous->previous;
            if (stp->key == st_->key) {
                st_->repetition = stp->repetition ? -i : i;
                break;
            }
        }
    }
    assert(pos_is_ok());
}

void Position::undo_move(Move m) {
    sideToMove_ = ~sideToMove_;

    Color us = sideToMove_;
    Square from = m.from(), to = m.to();
    Piece pc = piece_on(to);

    if (m.type() == PROMOTION) {
        remove_piece(to);
        put_piece(make_piece(us, PAWN), to);
        pc = make_piece(us, PAWN);
    }

    if (m.type() == CASTLING) {
        Square rfrom, rto;
        do_castling<false>(us, from, to, rfrom, rto);
    } else {
        move_piece(to, from);

        if (st_->capturedPiece) {
            Square capsq = to;
            if (m.type() == EN_PASSANT) capsq -= pawn_push(us);
            put_piece(st_->capturedPiece, capsq);
        }
    }
    (void)pc;

    st_ = st_->previous;
    gamePly_--;
    assert(pos_is_ok());
}

template <bool Do>
void Position::do_castling(Color us, Square from, Square& to, Square& rfrom, Square& rto) {
    bool kingSide = to > from;
    rfrom = to;
    rto   = relative_square(us, kingSide ? SQ_F1 : SQ_D1);
    to    = relative_square(us, kingSide ? SQ_G1 : SQ_C1);

    if (Do) {
        auto& dp = st_->dirtyPiece;
        dp.dirty_num = 2;
        dp.piece[0] = make_piece(us, KING);  dp.from[0] = from;  dp.to[0] = to;
        dp.piece[1] = make_piece(us, ROOK);  dp.from[1] = rfrom; dp.to[1] = rto;
    }

    remove_piece(Do ? from : to);
    remove_piece(Do ? rfrom : rto);
    board_[Do ? from : to] = board_[Do ? rfrom : rto] = NO_PIECE;
    put_piece(make_piece(us, KING), Do ? to : from);
    put_piece(make_piece(us, ROOK), Do ? rto : rfrom);
}

void Position::do_null_move(StateInfo& newSt) {
    assert(!checkers());
    assert(&newSt != st_);

    std::memcpy(&newSt, st_, offsetof(StateInfo, accumulator));
    newSt.previous = st_;
    newSt.accumulator.computed[WHITE] = newSt.accumulator.computed[BLACK] = false;
    newSt.dirtyPiece.dirty_num = 0;
    st_ = &newSt;

    if (st_->epSquare != SQ_NONE) {
        st_->key ^= Zobrist::enpassant[file_of(st_->epSquare)];
        st_->epSquare = SQ_NONE;
    }

    st_->key ^= Zobrist::side;
    st_->rule50++;
    st_->pliesFromNull = 0;

    sideToMove_ = ~sideToMove_;
    set_check_info();
    st_->repetition = 0;
    assert(pos_is_ok());
}

void Position::undo_null_move() {
    st_ = st_->previous;
    sideToMove_ = ~sideToMove_;
}

Key Position::key_after(Move m) const {
    Square from = m.from(), to = m.to();
    Piece pc = piece_on(from);
    Piece captured = piece_on(to);
    Key k = st_->key ^ Zobrist::side;

    if (captured) k ^= Zobrist::psq[captured][to];
    return k ^ Zobrist::psq[pc][to] ^ Zobrist::psq[pc][from];
}

bool Position::see_ge(Move m, Value threshold) const {
    if (m.type() != NORMAL) return VALUE_ZERO >= threshold;

    Square from = m.from(), to = m.to();

    int swap = PieceValue[type_of(piece_on(to))] - threshold;
    if (swap < 0) return false;

    swap = PieceValue[type_of(piece_on(from))] - swap;
    if (swap <= 0) return true;

    Bitboard occ = (occupied_ ^ square_bb(from)) | square_bb(to);
    Color stm = sideToMove_;
    Bitboard attackers = attackers_to(to, occ);
    Bitboard stmAttackers, bb;
    int res = 1;

    while (true) {
        stm = ~stm;
        attackers &= occ;

        if (!(stmAttackers = attackers & pieces(stm))) break;

        if (pinners(~stm) & occ) {
            stmAttackers &= ~blockers_for_king(stm);
            if (!stmAttackers) break;
        }

        res ^= 1;

        if ((bb = stmAttackers & pieces(PAWN))) {
            if ((swap = PieceValue[PAWN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= attacks_bb<BISHOP>(to, occ) & pieces(BISHOP, QUEEN);
        } else if ((bb = stmAttackers & pieces(KNIGHT))) {
            if ((swap = PieceValue[KNIGHT] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
        } else if ((bb = stmAttackers & pieces(BISHOP))) {
            if ((swap = PieceValue[BISHOP] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= attacks_bb<BISHOP>(to, occ) & pieces(BISHOP, QUEEN);
        } else if ((bb = stmAttackers & pieces(ROOK))) {
            if ((swap = PieceValue[ROOK] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= attacks_bb<ROOK>(to, occ) & pieces(ROOK, QUEEN);
        } else if ((bb = stmAttackers & pieces(QUEEN))) {
            if ((swap = PieceValue[QUEEN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= (attacks_bb<BISHOP>(to, occ) & pieces(BISHOP, QUEEN))
                       | (attacks_bb<ROOK>(to, occ) & pieces(ROOK, QUEEN));
        } else {

            return (attackers & ~pieces(stm)) ? bool(res ^ 1) : bool(res);
        }
    }
    return bool(res);
}

bool Position::is_draw(int ply) const {
    if (st_->rule50 > 99 && (!checkers() || MoveList<LEGAL>(*this).size()))
        return true;

    return st_->repetition && st_->repetition < ply;
}

bool Position::has_repeated() const {
    StateInfo* stc = st_;
    int end = std::min(st_->rule50, st_->pliesFromNull);
    while (end-- >= 4) {
        if (stc->repetition) return true;
        stc = stc->previous;
    }
    return false;
}

bool Position::upcoming_repetition(int ply) const {
    int end = std::min(st_->rule50, st_->pliesFromNull);
    if (end < 3) return false;

    Key originalKey = st_->key;
    StateInfo* stp = st_->previous;
    Key other = originalKey ^ stp->key ^ Zobrist::side;

    for (int i = 3; i <= end; i += 2) {
        stp = stp->previous;
        other ^= stp->key ^ stp->previous->key ^ Zobrist::side;
        stp = stp->previous;

        if (other != 0) continue;

        Key moveKey = originalKey ^ stp->key;
        int j = H1(moveKey);
        if (cuckoo[j] != moveKey) {
            j = H2(moveKey);
            if (cuckoo[j] != moveKey) continue;
        }

        Move move = cuckooMove[j];
        Square s1 = move.from(), s2 = move.to();

        if (BetweenBB[s1][s2] & ~square_bb(s2) & occupied_) continue;
        if (ply > i) return true;

        if (stp->repetition) return true;
    }
    return false;
}

bool Position::pos_is_ok() const {
#ifdef NDEBUG
    return true;
#else
    if (pieceCount_[W_KING] != 1 || pieceCount_[B_KING] != 1) return false;
    if (attackers_to(king_square(~sideToMove_)) & pieces(sideToMove_)) return false;
    if ((pieces(WHITE) & pieces(BLACK)) || (pieces(WHITE) | pieces(BLACK)) != occupied_) return false;

    for (PieceType p1 = PAWN; p1 <= KING; p1 = PieceType(int(p1) + 1))
        for (PieceType p2 = PieceType(int(p1) + 1); p2 <= KING; p2 = PieceType(int(p2) + 1))
            if (byType_[p1] & byType_[p2]) return false;

    if (pieces(PAWN) & (Rank1BB | Rank8BB)) return false;
    return true;
#endif
}

std::string Position::to_string() const {
    std::ostringstream os;
    os << "\n +---+---+---+---+---+---+---+---+\n";
    for (Rank r = RANK_8; r >= RANK_1; r = Rank(int(r) - 1)) {
        for (File f = FILE_A; f <= FILE_H; f = File(int(f) + 1)) {
            Piece pc = piece_on(make_square(f, r));
            os << " | " << (pc == NO_PIECE ? ' ' : PieceToChar[pc]);
        }
        os << " | " << (1 + int(r)) << "\n +---+---+---+---+---+---+---+---+\n";
    }
    os << "   a   b   c   d   e   f   g   h\n\nFen: " << fen()
       << "\nKey: " << std::hex << st_->key << std::dec << "\n";
    return os.str();
}

std::ostream& operator<<(std::ostream& os, const Position& pos) { return os << pos.to_string(); }

}
