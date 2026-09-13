#include "misc.h"
#include "movegen.h"
#include "nnue/network.h"
#include "position.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <random>

namespace blitz {

int nnue_check(int games, int maxPlies, u64 seed) {
    if (!nnue::available()) {
        std::printf("nnuecheck: no network loaded (set EvalFile first)\n");
        return 1;
    }

    std::mt19937_64 rng(seed);
    u64 compared = 0, mismatches = 0;
    int castles = 0, promos = 0, eps = 0, kingMoves = 0;

    for (int g = 0; g < games; ++g) {
        Position pos;
        std::deque<StateInfo> states;
        states.emplace_back();
        pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false, &states.back());
        nnue::refresh_accumulator(pos, pos.state());

        for (int ply = 0; ply < maxPlies; ++ply) {
            MoveList<LEGAL> legal(pos);
            if (legal.size() == 0 || pos.rule50_count() > 99) break;

            Move m = (legal.begin() + rng() % legal.size())->move;
            if (m.type() == CASTLING) castles++;
            if (m.type() == PROMOTION) promos++;
            if (m.type() == EN_PASSANT) eps++;
            if (type_of(pos.moved_piece(m)) == KING) kingMoves++;

            states.emplace_back();
            pos.do_move(m, states.back());

            Value inc = nnue::evaluate(pos);
            Value ref = nnue::evaluate_from_scratch(pos);
            compared++;
            if (inc != ref) {
                if (mismatches < 5)
                    std::printf("MISMATCH after %s: incremental=%d rebuilt=%d\n  %s\n",
                                move_to_uci(m).c_str(), inc, ref, pos.fen().c_str());
                mismatches++;
            }
        }
    }

    std::printf("nnuecheck: %llu positions compared, %llu mismatches\n"
                "  coverage: %d castles, %d promotions, %d en passant, %d king moves\n",
                (unsigned long long)compared, (unsigned long long)mismatches,
                castles, promos, eps, kingMoves);
    return mismatches != 0;
}

}
