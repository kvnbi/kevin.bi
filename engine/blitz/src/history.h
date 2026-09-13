#pragma once
#include "types.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <type_traits>

namespace blitz {

template <int D>
class StatsEntry {
    i16 v_ = 0;
public:
    void operator=(int v) { v_ = i16(v); }
    operator int() const { return v_; }

    void operator<<(int bonus) {
        static_assert(D <= 32767, "history limit must fit in i16");
        bonus = std::clamp(bonus, -D, D);
        v_ += i16(bonus - v_ * std::abs(bonus) / D);
        assert(std::abs(int(v_)) <= D);
    }
};

template <typename T, int D, int Size, int... Sizes>
struct StatsHelper {
    using type = std::array<typename StatsHelper<T, D, Sizes...>::type, Size>;
};

template <typename T, int D, int Size>
struct StatsHelper<T, D, Size> {
    using leaf = std::conditional_t<std::is_same_v<T, i16>, StatsEntry<D>, T>;
    using type = std::array<leaf, Size>;
};

template <typename T, int D, int... Sizes>
struct Stats : StatsHelper<T, D, Sizes...>::type {

    void fill(i16 v) {
        i16* p = reinterpret_cast<i16*>(this);
        std::fill(p, p + sizeof(*this) / sizeof(i16), v);
    }
};

using ButterflyHistory = Stats<i16, 7183, COLOR_NB, int(SQUARE_NB) * int(SQUARE_NB)>;

using CapturePieceToHistory = Stats<i16, 10692, PIECE_NB, SQUARE_NB, PIECE_TYPE_NB>;

using PieceToHistory = Stats<i16, 29952, PIECE_NB, SQUARE_NB>;

using ContinuationHistory = Stats<PieceToHistory, 0, PIECE_NB, SQUARE_NB>;

constexpr int PAWN_HISTORY_SIZE = 512;
using PawnHistory = Stats<i16, 8192, PAWN_HISTORY_SIZE, PIECE_NB, SQUARE_NB>;

constexpr int CORRECTION_HISTORY_SIZE  = 16384;
constexpr int CORRECTION_HISTORY_LIMIT = 1024;
using CorrectionHistory = Stats<i16, CORRECTION_HISTORY_LIMIT, COLOR_NB, CORRECTION_HISTORY_SIZE>;

inline int pawn_structure_index(Key pawnKey) { return int(pawnKey & (PAWN_HISTORY_SIZE - 1)); }
inline int correction_index(Key key) { return int(key & (CORRECTION_HISTORY_SIZE - 1)); }

inline int stat_bonus(int d) { return std::min(168 * d - 100, 1718); }
inline int stat_malus(int d) { return std::min(768 * d - 257, 2351); }

}
