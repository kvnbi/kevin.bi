#include "position.h"
#include "movegen.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace blitz;

template <bool Root>
u64 perft(Position& pos, int depth) {
    StateInfo st;
    u64 nodes = 0, cnt;
    const bool leaf = (depth == 2);

    for (const auto& m : MoveList<LEGAL>(pos)) {
        if (Root && depth <= 1) {
            cnt = 1;
            nodes++;
        } else {
            pos.do_move(m.move, st);
            cnt = leaf ? MoveList<LEGAL>(pos).size() : perft<false>(pos, depth - 1);
            nodes += cnt;
            pos.undo_move(m.move);
        }
        if (Root) std::printf("%s: %llu\n", move_to_uci(m.move, pos.is_chess960()).c_str(),
                              (unsigned long long)cnt);
    }
    return nodes;
}

struct Case { const char* fen; int depth; u64 expected; bool frc; };

int main(int argc, char** argv) {
    bitboards_init();
    Position::init();

    if (argc >= 3) {
        Position pos; StateInfo st;
        pos.set(argv[1], false, &st);
        u64 n = perft<true>(pos, std::atoi(argv[2]));
        std::printf("\nNodes: %llu\n", (unsigned long long)n);
        return 0;
    }

    static const Case cases[] = {

        { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324, false },
        { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690, false },
        { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7, 178633661, false },
        { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 706045033, false },
        { "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194, false },
        { "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 6, 6923051137ULL, false },

        { "8/8/8/8/k1p4R/8/3P4/3K4 w - - 0 1", 6, 1134888, false },
        { "8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133, false },
        { "8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467, false },
        { "5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072, false },
        { "3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711, false },
        { "r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206, false },
        { "r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476, false },
        { "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001, false },
        { "8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658, false },
        { "4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342, false },
        { "8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683, false },
        { "K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217, false },
        { "8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584, false },
        { "8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527, false },

        { "bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9", 5, 8146062, true },
        { "2nnrbkr/p1qppppp/8/1ppb4/6PP/3PP3/PPP2P2/BQNNRBKR w HEhe - 1 9", 5, 16253601, true },
        { "b1q1rrkb/pppppppp/3nn3/8/P7/1PPP4/4PPPP/BQNNRKRB w GE - 1 9", 5, 6417013, true },
        { "qbbnnrkr/2pp2pp/p7/1p2pp2/8/P3PP2/1PPP1KPP/QBBNNR1R w hf - 0 9", 5, 9183776, true },
    };

    int failures = 0;
    u64 total = 0;
    auto t0 = std::chrono::steady_clock::now();

    for (const Case& c : cases) {
        Position pos; StateInfo st;
        pos.set(c.fen, c.frc, &st);
        u64 got = perft<false>(pos, c.depth);
        total += got;
        bool ok = (got == c.expected);
        failures += !ok;
        std::printf("%-5s depth %d  %14llu  %s\n", ok ? "ok" : "FAIL", c.depth,
                    (unsigned long long)got, c.fen);
        if (!ok) std::printf("        expected %llu\n", (unsigned long long)c.expected);
        std::fflush(stdout);
    }

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    std::printf("\n%llu nodes in %lld ms  (%.2f Mnps)   failures: %d\n",
                (unsigned long long)total, (long long)ms,
                ms ? double(total) / double(ms) / 1000.0 : 0.0, failures);
    return failures != 0;
}
