#include "datagen.h"
#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "nnue/network.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <vector>

namespace blitz::datagen {

Sample encode(const Position& pos, Value score) {
    Sample s{};
    s.occ = pos.pieces();
    std::memset(s.pieces, 0xFF, sizeof(s.pieces));

    int i = 0;
    for (Bitboard b = pos.pieces(); b; ++i) {
        Square sq = pop_lsb(b);
        Piece  pc = pos.piece_on(sq);
        u8 code = u8(int(color_of(pc)) * 6 + int(type_of(pc)) - 1);
        if (i % 2 == 0) s.pieces[i / 2] = u8((s.pieces[i / 2] & 0xF0) | code);
        else            s.pieces[i / 2] = u8((s.pieces[i / 2] & 0x0F) | (code << 4));
    }
    s.score = i16(std::clamp(int(score), -30000, 30000));
    s.stm = u8(pos.side_to_move());
    s.result = 1;
    return s;
}

namespace {

std::mutex g_fileMutex;
std::atomic<u64> g_written{0};
std::atomic<u64> g_games{0};

void play_game(std::vector<Sample>& out, std::mt19937_64& rng, u64 nodeLimit) {
    Position pos;
    std::deque<StateInfo> states;
    states.emplace_back();
    pos.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false, &states.back());

    int randomPlies = 8 + int(rng() % 2);
    for (int i = 0; i < randomPlies; ++i) {
        MoveList<LEGAL> legal(pos);
        if (legal.size() == 0) return;
        Move m = (legal.begin() + rng() % legal.size())->move;
        states.emplace_back();
        pos.do_move(m, states.back());
    }
    if (MoveList<LEGAL>(pos).size() == 0) return;

    Search::clear();
    if (std::abs(Eval::evaluate(pos)) > 600) return;

    std::vector<Sample> pending;
    int result = 1;
    int plies = 0;
    int decidedPlies = 0;

    constexpr Value AdjudicateScore = 2500 * EVAL_SCALE;
    constexpr int   AdjudicatePlies = 12;
    constexpr Value RecordScoreCap  = 4000 * EVAL_SCALE;

    while (true) {
        if (pos.is_draw(0) || MoveList<LEGAL>(pos).size() == 0) {
            if (MoveList<LEGAL>(pos).size() == 0 && pos.checkers())
                result = (pos.side_to_move() == WHITE) ? 0 : 2;
            break;
        }
        if (++plies > 400) break;

        Search::LimitsType limits;
        limits.startTime = now();
        limits.nodes = nodeLimit;

        Threads.start_thinking(pos, nullptr, limits, false);
        Threads.main()->wait_for_search_finished();

        const auto& rm = Threads.main()->rootMoves[0];
        Move  best = rm.pv[0];
        Value score = rm.score;
        if (best == Move::none()) break;

        bool stmWinning = score > 0;
        bool whiteWins = (pos.side_to_move() == WHITE) == stmWinning;

        decidedPlies = (std::abs(score) >= AdjudicateScore) ? decidedPlies + 1 : 0;

        if (!pos.checkers() && !pos.capture_stage(best))
            pending.push_back(encode(pos, std::clamp(score, -RecordScoreCap, RecordScoreCap)));

        if (decidedPlies >= AdjudicatePlies) {
            result = whiteWins ? 2 : 0;
            break;
        }

        states.emplace_back();
        pos.do_move(best, states.back());
    }

    for (Sample& s : pending) {

        int r = (s.stm == u8(WHITE)) ? result : 2 - result;
        s.result = u8(r);
        out.push_back(s);
    }
    g_games.fetch_add(1, std::memory_order_relaxed);
}

}

void run(int argc, char** argv) {
    if (argc < 4) {
        std::printf("usage: blitz datagen <out.bin> <positions> [nodes-per-move] [net|none]\n"
                    "  net: path to an NNUE network, or 'none' to force the hand-crafted\n"
                    "       evaluation.  Defaults to whatever EvalFile loaded at startup.\n");
        return;
    }
    const char* path = argv[2];
    u64 target = std::strtoull(argv[3], nullptr, 10);
    u64 nodeLimit = (argc > 4) ? std::strtoull(argv[4], nullptr, 10) : 5000;

    if (argc > 5) {
        std::string net = argv[5];
        if (net == "none") {
            nnue::unload();
        } else if (!nnue::load(net)) {
            std::printf("datagen: could not load network %s\n", net.c_str());
            return;
        }
    }
    std::printf("datagen: evaluator = %s\n", nnue::available() ? "NNUE" : "hand-crafted");

    std::FILE* f = std::fopen(path, "ab");
    if (!f) { std::printf("cannot open %s\n", path); return; }

    Search::Silent = true;
    std::mt19937_64 rng(std::random_device{}());
    std::vector<Sample> buffer;
    TimePoint t0 = now();

    while (g_written.load() < target) {
        buffer.clear();
        play_game(buffer, rng, nodeLimit);
        if (buffer.empty()) continue;

        {
            std::lock_guard lk(g_fileMutex);
            std::fwrite(buffer.data(), sizeof(Sample), buffer.size(), f);
        }
        u64 n = g_written.fetch_add(buffer.size()) + buffer.size();

        if ((g_games.load() % 50) == 0) {
            TimePoint dt = std::max<TimePoint>(1, now() - t0);
            std::printf("\r%llu positions, %llu games, %.0f pos/s      ",
                        (unsigned long long)n, (unsigned long long)g_games.load(),
                        double(n) * 1000.0 / double(dt));
            std::fflush(stdout);
        }
    }

    std::fclose(f);
    std::printf("\ndone: %llu positions written to %s\n",
                (unsigned long long)g_written.load(), path);
}

}
