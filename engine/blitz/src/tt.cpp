#include "tt.h"
#include "position.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace blitz {

TranspositionTable TT;

void TTEntry::save(Key k, Value v, bool pv, Bound b, int d, Move m, Value ev, u8 generation) {

    if (m != Move::none() || u16(k) != key16) move16 = m.raw();

    if (b == BOUND_EXACT || u16(k) != key16 || d - DEPTH_OFFSET + 2 * pv > depth8 - 4) {
        assert(d > DEPTH_OFFSET);
        key16     = u16(k);
        depth8    = u8(d - DEPTH_OFFSET);
        genBound8 = u8(generation | (u8(pv) << 2) | b);
        value16   = i16(v);
        eval16    = i16(ev);
    }
}

TranspositionTable::~TranspositionTable() { aligned_large_pages_free(table_); }

void TranspositionTable::resize(size_t mbSize, int threads) {
    aligned_large_pages_free(table_);

    clusterCount_ = mbSize * 1024 * 1024 / sizeof(Cluster);
    table_ = static_cast<Cluster*>(aligned_large_pages_alloc(clusterCount_ * sizeof(Cluster)));
    if (!table_) {
        std::fprintf(stderr, "Failed to allocate %zu MB for the transposition table.\n", mbSize);
        std::exit(EXIT_FAILURE);
    }
    clear(threads);
}

void TranspositionTable::clear(int threads) {
    std::vector<std::thread> workers;
    for (int i = 0; i < threads; ++i)
        workers.emplace_back([this, i, threads]() {
            size_t begin = clusterCount_ * i / threads;
            size_t end   = clusterCount_ * (i + 1) / threads;
            std::memset(table_ + begin, 0, (end - begin) * sizeof(Cluster));
        });
    for (auto& t : workers) t.join();
    generation_ = 0;
}

std::pair<bool, TTEntry*> TranspositionTable::probe(Key key) const {
    TTEntry* const tte = first_entry(key);
    const u16 key16 = u16(key);

    for (int i = 0; i < Cluster::Size; ++i)
        if (tte[i].key16 == key16 || !tte[i].depth8) {

            tte[i].genBound8 = u8(generation_ | (tte[i].genBound8 & 0x7));
            return { bool(tte[i].depth8), &tte[i] };
        }

    TTEntry* replace = tte;
    for (int i = 1; i < Cluster::Size; ++i)
        if (tte[i].worth(generation_) < replace->worth(generation_)) replace = &tte[i];

    return { false, replace };
}

int TranspositionTable::hashfull() const {
    int cnt = 0;
    for (int i = 0; i < 1000; ++i)
        for (int j = 0; j < Cluster::Size; ++j)
            cnt += table_[i].entry[j].depth8 && (table_[i].entry[j].genBound8 & 0xF8) == generation_;
    return cnt / Cluster::Size;
}

}
