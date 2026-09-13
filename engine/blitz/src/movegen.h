#pragma once
#include "types.h"
#include <algorithm>

namespace blitz {

class Position;

enum GenType {
    CAPTURES,
    QUIETS,
    EVASIONS,
    NON_EVASIONS,
    LEGAL
};

struct ExtMove {
    Move move;
    int  value;

    operator Move() const { return move; }
    void operator=(Move m) { move = m; }
    explicit operator bool() const = delete;
};

inline bool operator<(const ExtMove& a, const ExtMove& b) { return a.value < b.value; }

template <GenType T>
ExtMove* generate(const Position& pos, ExtMove* moveList);

template <GenType T>
struct MoveList {
    explicit MoveList(const Position& pos) : last_(generate<T>(pos, moves_)) {}

    const ExtMove* begin() const { return moves_; }
    const ExtMove* end() const { return last_; }
    size_t size() const { return size_t(last_ - moves_); }
    bool contains(Move m) const {
        for (const ExtMove* it = begin(); it != end(); ++it)
            if (it->move == m) return true;
        return false;
    }

private:
    ExtMove moves_[MAX_MOVES], *last_;
};

}
