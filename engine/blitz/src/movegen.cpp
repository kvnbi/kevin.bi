#include "movegen.h"
#include "position.h"

namespace blitz {

namespace {

template <Direction D>
ExtMove* make_promotions(ExtMove* moveList, Square to) {
    Square from = to - D;
    *moveList++ = Move::make<PROMOTION>(from, to, QUEEN);
    *moveList++ = Move::make<PROMOTION>(from, to, ROOK);
    *moveList++ = Move::make<PROMOTION>(from, to, BISHOP);
    *moveList++ = Move::make<PROMOTION>(from, to, KNIGHT);
    return moveList;
}

template <Color Us, GenType Type>
ExtMove* generate_pawn_moves(const Position& pos, ExtMove* moveList, Bitboard target) {
    constexpr Color     Them     = ~Us;
    constexpr Bitboard  TRank7BB = (Us == WHITE ? Rank7BB : Rank2BB);
    constexpr Bitboard  TRank3BB = (Us == WHITE ? Rank3BB : Rank6BB);
    constexpr Direction Up       = pawn_push(Us);
    constexpr Direction UpRight  = (Us == WHITE ? NORTH_EAST : SOUTH_WEST);
    constexpr Direction UpLeft   = (Us == WHITE ? NORTH_WEST : SOUTH_EAST);

    const Bitboard emptySquares = ~pos.pieces();
    const Bitboard enemies      = (Type == EVASIONS) ? pos.checkers() : pos.pieces(Them);

    Bitboard pawnsOn7    = pos.pieces(Us, PAWN) & TRank7BB;
    Bitboard pawnsNotOn7 = pos.pieces(Us, PAWN) & ~TRank7BB;

    if constexpr (Type != CAPTURES) {
        Bitboard b1 = shift<Up>(pawnsNotOn7) & emptySquares;
        Bitboard b2 = shift<Up>(b1 & TRank3BB) & emptySquares;

        if constexpr (Type == EVASIONS) {
            b1 &= target;
            b2 &= target;
        }

        while (b1) { Square to = pop_lsb(b1); *moveList++ = Move(to - Up, to); }
        while (b2) { Square to = pop_lsb(b2); *moveList++ = Move(to - Up - Up, to); }
    }

    if constexpr (Type != QUIETS) {
        if (pawnsOn7) {
            Bitboard b1 = shift<UpRight>(pawnsOn7) & enemies;
            Bitboard b2 = shift<UpLeft>(pawnsOn7) & enemies;
            Bitboard b3 = shift<Up>(pawnsOn7) & emptySquares;

            if constexpr (Type == EVASIONS) b3 &= target;

            while (b1) moveList = make_promotions<UpRight>(moveList, pop_lsb(b1));
            while (b2) moveList = make_promotions<UpLeft>(moveList, pop_lsb(b2));
            while (b3) moveList = make_promotions<Up>(moveList, pop_lsb(b3));
        }
    }

    if constexpr (Type != QUIETS) {
        Bitboard b1 = shift<UpRight>(pawnsNotOn7) & enemies;
        Bitboard b2 = shift<UpLeft>(pawnsNotOn7) & enemies;

        while (b1) { Square to = pop_lsb(b1); *moveList++ = Move(to - UpRight, to); }
        while (b2) { Square to = pop_lsb(b2); *moveList++ = Move(to - UpLeft, to); }

        if (pos.ep_square() != SQ_NONE) {
            Square epSq  = pos.ep_square();
            Square capsq = epSq - Up;

            bool useful = (Type != EVASIONS)
                       || (pos.checkers() & square_bb(capsq))
                       || (target & square_bb(epSq));

            if (useful) {
                Bitboard b = pawnsNotOn7 & PawnAttacks[Them][epSq];
                while (b) *moveList++ = Move::make<EN_PASSANT>(pop_lsb(b), epSq);
            }
        }
    }
    return moveList;
}

template <Color Us, PieceType Pt>
ExtMove* generate_moves(const Position& pos, ExtMove* moveList, Bitboard target) {
    static_assert(Pt != KING && Pt != PAWN, "handled separately");

    Bitboard bb = pos.pieces(Us, Pt);
    while (bb) {
        Square from = pop_lsb(bb);
        Bitboard b = attacks_bb(Pt, from, pos.pieces()) & target;
        while (b) *moveList++ = Move(from, pop_lsb(b));
    }
    return moveList;
}

template <Color Us, GenType Type>
ExtMove* generate_all(const Position& pos, ExtMove* moveList) {
    const Square ksq = pos.king_square(Us);

    if (Type != EVASIONS || !more_than_one(pos.checkers())) {
        Bitboard target = (Type == EVASIONS)     ? BetweenBB[ksq][lsb(pos.checkers())]
                        : (Type == NON_EVASIONS) ? ~pos.pieces(Us)
                        : (Type == CAPTURES)     ? pos.pieces(~Us)
                                                 : ~pos.pieces();

        moveList = generate_pawn_moves<Us, Type>(pos, moveList, target);
        moveList = generate_moves<Us, KNIGHT>(pos, moveList, target);
        moveList = generate_moves<Us, BISHOP>(pos, moveList, target);
        moveList = generate_moves<Us, ROOK>(pos, moveList, target);
        moveList = generate_moves<Us, QUEEN>(pos, moveList, target);
    }

    Bitboard b = attacks_bb<KING>(ksq)
               & ((Type == EVASIONS)   ? ~pos.pieces(Us)
                : (Type == CAPTURES)   ? pos.pieces(~Us)
                : (Type == QUIETS)     ? ~pos.pieces()
                                       : ~pos.pieces(Us));
    while (b) *moveList++ = Move(ksq, pop_lsb(b));

    if constexpr (Type == QUIETS || Type == NON_EVASIONS) {
        for (CastlingRights cr : { Us == WHITE ? WHITE_OO : BLACK_OO,
                                   Us == WHITE ? WHITE_OOO : BLACK_OOO })
            if (pos.can_castle(cr) && !pos.castling_impeded(cr))
                *moveList++ = Move::make<CASTLING>(ksq, pos.castling_rook_square(cr));
    }
    return moveList;
}

}

template <GenType Type>
ExtMove* generate(const Position& pos, ExtMove* moveList) {
    static_assert(Type != LEGAL, "LEGAL has its own specialisation");
    assert((Type == EVASIONS) == bool(pos.checkers()));

    return pos.side_to_move() == WHITE ? generate_all<WHITE, Type>(pos, moveList)
                                       : generate_all<BLACK, Type>(pos, moveList);
}

template ExtMove* generate<CAPTURES>(const Position&, ExtMove*);
template ExtMove* generate<QUIETS>(const Position&, ExtMove*);
template ExtMove* generate<EVASIONS>(const Position&, ExtMove*);
template ExtMove* generate<NON_EVASIONS>(const Position&, ExtMove*);

template <>
ExtMove* generate<LEGAL>(const Position& pos, ExtMove* moveList) {
    Color    us     = pos.side_to_move();
    Bitboard pinned = pos.blockers_for_king(us) & pos.pieces(us);
    Square   ksq    = pos.king_square(us);

    ExtMove* cur = moveList;
    moveList = pos.checkers() ? generate<EVASIONS>(pos, moveList)
                              : generate<NON_EVASIONS>(pos, moveList);
    while (cur != moveList) {
        if (((pinned & square_bb(cur->move.from())) || cur->move.from() == ksq
             || cur->move.type() == EN_PASSANT)
            && !pos.legal(cur->move))
            *cur = (--moveList)->move;
        else
            ++cur;
    }
    return moveList;
}

}
