#include "movepick.h"
#include "position.h"
#include <algorithm>
#include <limits>

namespace blitz {

namespace {

enum Stage {

    MAIN_TT, CAPTURE_INIT, GOOD_CAPTURE, REFUTATION, QUIET_INIT, GOOD_QUIET,
    BAD_CAPTURE, BAD_QUIET,

    EVASION_TT, EVASION_INIT, EVASION,

    PROBCUT_TT, PROBCUT_INIT, PROBCUT,

    QSEARCH_TT, QCAPTURE_INIT, QCAPTURE
};

void partial_insertion_sort(ExtMove* begin, ExtMove* end, int limit) {
    for (ExtMove *sortedEnd = begin, *p = begin + 1; p < end; ++p)
        if (p->value >= limit) {
            ExtMove tmp = *p, *q;
            *p = *++sortedEnd;
            for (q = sortedEnd; q != begin && *(q - 1) < tmp; --q) *q = *(q - 1);
            *q = tmp;
        }
}

}

MovePicker::MovePicker(const Position& pos, Move ttMove, int depth,
                       const ButterflyHistory* mh, const CapturePieceToHistory* cph,
                       const PieceToHistory** ch, const PawnHistory* ph,
                       const Move* killers)
    : pos_(pos), mainHistory_(mh), captureHistory_(cph), continuation_(ch),
      pawnHistory_(ph), ttMove_(ttMove), depth_(depth) {

    if (killers) {
        refutations_[0] = killers[0];
        refutations_[1] = killers[1];
    }

    if (pos.checkers())
        stage_ = EVASION_TT + !(ttMove && pos.pseudo_legal(ttMove));
    else
        stage_ = (depth > 0 ? MAIN_TT : QSEARCH_TT)
               + !(ttMove && pos.pseudo_legal(ttMove));
}

MovePicker::MovePicker(const Position& pos, Move ttMove, Value threshold,
                       const CapturePieceToHistory* cph)
    : pos_(pos), captureHistory_(cph), ttMove_(ttMove), depth_(0), threshold_(threshold) {

    assert(!pos.checkers());
    stage_ = PROBCUT_TT + !(ttMove && pos.capture_stage(ttMove)
                            && pos.pseudo_legal(ttMove) && pos.see_ge(ttMove, threshold));
}

template <GenType Type>
void MovePicker::score() {
    static_assert(Type == CAPTURES || Type == QUIETS || Type == EVASIONS, "unsupported");

    [[maybe_unused]] Bitboard threatByPawn = 0, threatByMinor = 0, threatByRook = 0;
    if constexpr (Type == QUIETS) {
        Color us = pos_.side_to_move();
        threatByPawn  = pos_.attacks_by(~us, PAWN);
        threatByMinor = pos_.attacks_by(~us, KNIGHT) | pos_.attacks_by(~us, BISHOP) | threatByPawn;
        threatByRook  = pos_.attacks_by(~us, ROOK) | threatByMinor;
    }

    for (ExtMove& m : *this) {
        const Square from = m.move.from(), to = m.move.to();
        const Piece  pc   = pos_.moved_piece(m.move);
        const PieceType pt = type_of(pc);

        if constexpr (Type == CAPTURES) {
            m.value = 7 * PieceValue[type_of(pos_.piece_on(to))]
                    + (*captureHistory_)[pc][to][type_of(pos_.piece_on(to))];
        } else if constexpr (Type == QUIETS) {
            m.value = 2 * (*mainHistory_)[pos_.side_to_move()][m.move.from_to()];
            m.value += 2 * (*pawnHistory_)[pawn_structure_index(pos_.pawn_key())][pc][to];
            m.value += (*continuation_[0])[pc][to];
            m.value += (*continuation_[1])[pc][to];
            m.value += (*continuation_[3])[pc][to] / 4;
            m.value += (*continuation_[5])[pc][to];

            Bitboard threatened = (pt == QUEEN) ? threatByRook
                                : (pt == ROOK)  ? threatByMinor
                                : (pt == KNIGHT || pt == BISHOP) ? threatByPawn : Bitboard(0);
            if (threatened) {
                m.value += (threatened & square_bb(from)) ? 16384 : 0;
                m.value -= (threatened & square_bb(to)) ? 16384 : 0;
            }

            if (pos_.check_squares(pt) & square_bb(to)) m.value += 16384;
        } else {
            if (pos_.capture(m.move))
                m.value = PieceValue[type_of(pos_.piece_on(to))] - PieceValue[pt] + (1 << 28);
            else
                m.value = (*mainHistory_)[pos_.side_to_move()][m.move.from_to()]
                        + (*continuation_[0])[pc][to];
        }
    }
}

Move MovePicker::next_move(bool skipQuiets) {
    auto select = [&](auto&& filter) -> Move {
        for (; cur_ < endMoves_; ++cur_)
            if (cur_->move != ttMove_ && filter()) return (cur_++)->move;
        return Move::none();
    };

    auto not_refutation = [&]() {
        return cur_->move != refutations_[0] && cur_->move != refutations_[1];
    };

top:
    switch (stage_) {
        case MAIN_TT:
        case EVASION_TT:
        case QSEARCH_TT:
        case PROBCUT_TT:
            ++stage_;
            return ttMove_;

        case CAPTURE_INIT:
        case PROBCUT_INIT:
        case QCAPTURE_INIT:
            cur_ = endBadCaptures_ = moves_;
            endMoves_ = generate<CAPTURES>(pos_, cur_);
            score<CAPTURES>();
            partial_insertion_sort(cur_, endMoves_, std::numeric_limits<int>::min());
            ++stage_;
            goto top;

        case GOOD_CAPTURE:

            if (Move m = select([&]() {
                    if (pos_.see_ge(cur_->move, -cur_->value / 18)) return true;
                    *endBadCaptures_++ = *cur_;
                    return false;
                }))
                return m;
            ++stage_;
            [[fallthrough]];

        case REFUTATION:
            while (refutationIdx_ < 2) {
                Move m = refutations_[refutationIdx_++];
                if (m != Move::none() && m != ttMove_ && !pos_.capture_stage(m)
                    && pos_.pseudo_legal(m))
                    return m;
            }
            ++stage_;
            [[fallthrough]];

        case QUIET_INIT:
            if (!skipQuiets) {
                cur_ = endBadCaptures_;
                endMoves_ = beginBadQuiets_ = generate<QUIETS>(pos_, cur_);
                score<QUIETS>();
                partial_insertion_sort(cur_, endMoves_, -3560 * depth_);
            }
            ++stage_;
            [[fallthrough]];

        case GOOD_QUIET:
            if (!skipQuiets) {
                if (Move m = select([&]() { return cur_->value > -8000 && not_refutation(); }))
                    return m;

                beginBadQuiets_ = cur_;
                endBadQuiets_ = endMoves_;
            }
            cur_ = moves_;
            endMoves_ = endBadCaptures_;
            ++stage_;
            [[fallthrough]];

        case BAD_CAPTURE:
            if (Move m = select([]() { return true; })) return m;
            cur_ = beginBadQuiets_;
            endMoves_ = endBadQuiets_;
            ++stage_;
            [[fallthrough]];

        case BAD_QUIET:
            if (!skipQuiets) return select(not_refutation);
            return Move::none();

        case EVASION_INIT:
            cur_ = moves_;
            endMoves_ = generate<EVASIONS>(pos_, cur_);
            score<EVASIONS>();
            partial_insertion_sort(cur_, endMoves_, std::numeric_limits<int>::min());
            ++stage_;
            [[fallthrough]];

        case EVASION:
            return select([]() { return true; });

        case PROBCUT:
            return select([&]() { return pos_.see_ge(cur_->move, threshold_); });

        case QCAPTURE:
            return select([]() { return true; });
    }
    assert(false);
    return Move::none();
}

}
