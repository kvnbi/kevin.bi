#pragma once
#include "history.h"
#include "movegen.h"
#include "types.h"

namespace blitz {

class Position;

class MovePicker {
public:
    MovePicker(const MovePicker&) = delete;
    MovePicker& operator=(const MovePicker&) = delete;

    MovePicker(const Position& pos, Move ttMove, int depth,
               const ButterflyHistory* mh, const CapturePieceToHistory* cph,
               const PieceToHistory** ch, const PawnHistory* ph,
               const Move* killers);

    MovePicker(const Position& pos, Move ttMove, Value threshold,
               const CapturePieceToHistory* cph);

    Move next_move(bool skipQuiets = false);

private:
    template <GenType> void score();
    ExtMove* begin() { return cur_; }
    ExtMove* end() { return endMoves_; }

    const Position& pos_;
    const ButterflyHistory*      mainHistory_    = nullptr;
    const CapturePieceToHistory* captureHistory_ = nullptr;
    const PieceToHistory**       continuation_   = nullptr;
    const PawnHistory*           pawnHistory_    = nullptr;

    Move  ttMove_;

    Move  refutations_[2] = { Move::none(), Move::none() };
    int   refutationIdx_ = 0;
    ExtMove *cur_ = moves_, *endMoves_ = moves_, *endBadCaptures_ = moves_;
    ExtMove *beginBadQuiets_ = moves_, *endBadQuiets_ = moves_;
    int   stage_;
    int   depth_;
    Value threshold_ = VALUE_ZERO;
    ExtMove moves_[MAX_MOVES];
};

}
