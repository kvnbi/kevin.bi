#pragma once
#include "types.h"
#include "misc.h"
#include <utility>

namespace blitz {

class Position;

constexpr int DEPTH_OFFSET   = -7;
constexpr int DEPTH_QS_CHECK =  0;
constexpr int DEPTH_QS_NO_CHECK = -1;
constexpr int DEPTH_NONE     = -6;

struct TTEntry {
    u16  key16;
    u8   depth8;
    u8   genBound8;
    u16  move16;
    i16  value16;
    i16  eval16;

    Move  move()  const { return Move(move16); }
    Value value() const { return Value(value16); }
    Value eval()  const { return Value(eval16); }
    int   depth() const { return int(depth8) + DEPTH_OFFSET; }
    bool  is_pv() const { return bool(genBound8 & 0x4); }
    Bound bound() const { return Bound(genBound8 & 0x3); }

    void save(Key k, Value v, bool pv, Bound b, int d, Move m, Value ev, u8 generation);

    u8 relative_age(u8 generation) const {
        return u8((263 + generation - genBound8) & 0xF8);
    }
    int worth(u8 generation) const { return depth8 - 2 * relative_age(generation); }
};

struct alignas(32) Cluster {
    static constexpr int Size = 3;
    TTEntry entry[Size];
    char    padding[2];
};
static_assert(sizeof(Cluster) == 32, "unexpected cluster size");

class TranspositionTable {
public:
    ~TranspositionTable();

    void resize(size_t mbSize, int threads);
    void clear(int threads);
    void new_search() { generation_ += 8; }
    u8   generation() const { return generation_; }
    int  hashfull() const;

    std::pair<bool, TTEntry*> probe(Key key) const;
    void prefetch_entry(Key key) const { prefetch(first_entry(key)); }

private:
    TTEntry* first_entry(Key key) const {

        return &table_[(u64((__uint128_t(key) * __uint128_t(clusterCount_)) >> 64))].entry[0];
    }

    Cluster* table_ = nullptr;
    size_t   clusterCount_ = 0;
    u8       generation_ = 0;
};

inline Value value_to_tt(Value v, int ply) {
    return v >= VALUE_MATE_IN_MAX_PLY  ? Value(v + ply)
         : v <= VALUE_MATED_IN_MAX_PLY ? Value(v - ply)
                                       : v;
}

inline Value value_from_tt(Value v, int ply, int rule50) {
    if (v == VALUE_NONE) return VALUE_NONE;

    if (v >= VALUE_MATE_IN_MAX_PLY) {

        if (VALUE_MATE - v > 99 - rule50) return VALUE_MATE_IN_MAX_PLY - 1;
        return Value(v - ply);
    }
    if (v <= VALUE_MATED_IN_MAX_PLY) {
        if (VALUE_MATE + v > 99 - rule50) return VALUE_MATED_IN_MAX_PLY + 1;
        return Value(v + ply);
    }
    return v;
}

extern TranspositionTable TT;

}
