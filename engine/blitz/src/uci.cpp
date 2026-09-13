#include "uci.h"
#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "nnue/network.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <sstream>

namespace blitz {

void benchmark(Position& pos, std::istream& is);
int  nnue_check(int games, int maxPlies, u64 seed);

std::ostream& operator<<(std::ostream& os, SyncCout sc) {
    static std::mutex m;
    if (sc == IO_LOCK) m.lock();
    if (sc == IO_UNLOCK) m.unlock();
    return os;
}

namespace {

constexpr auto StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

std::deque<StateInfo> g_states;

struct OptionValues {
    int  hash = 256;
    int  threads = 1;
    int  multiPV = 1;
    int  moveOverhead = 30;
    bool chess960 = false;
    bool ponder = false;
    bool showWDL = false;
    std::string evalFile = nnue::EmbeddedName;
} g_opt;

u64 perft_nodes(Position& pos, int depth) {
    if (depth == 0) return 1;
    StateInfo st;
    u64 nodes = 0;
    for (const auto& m : MoveList<LEGAL>(pos)) {
        if (depth == 1) { nodes++; continue; }
        pos.do_move(m.move, st);
        nodes += perft_nodes(pos, depth - 1);
        pos.undo_move(m.move);
    }
    return nodes;
}

void position_cmd(Position& pos, std::istringstream& is) {
    Threads.stop = true;
    Threads.main()->wait_for_search_finished();

    std::string token, fen;
    is >> token;

    if (token == "startpos") { fen = StartFEN; is >> token; }
    else if (token == "fen") { while (is >> token && token != "moves") fen += token + " "; }
    else return;

    g_states.clear();
    g_states.emplace_back();
    pos.set(fen, g_opt.chess960, &g_states.back());

    while (is >> token) {
        Move m = UCI::to_move(pos, token);
        if (m == Move::none()) break;
        g_states.emplace_back();
        pos.do_move(m, g_states.back());
    }
}

void go_cmd(Position& pos, std::istringstream& is) {
    Search::LimitsType limits;
    limits.startTime = now();
    std::string token;
    bool ponderMode = false;

    bool inSearchmoves = false;
    while (is >> token) {
        if (inSearchmoves) {
            Move m = UCI::to_move(pos, token);
            if (m != Move::none()) { limits.searchmoves.push_back(m); continue; }
            inSearchmoves = false;
        }
        if (token == "searchmoves")    inSearchmoves = true;
        else if (token == "wtime")     is >> limits.time[WHITE];
        else if (token == "btime")     is >> limits.time[BLACK];
        else if (token == "winc")      is >> limits.inc[WHITE];
        else if (token == "binc")      is >> limits.inc[BLACK];
        else if (token == "movestogo") is >> limits.movestogo;
        else if (token == "depth")     { is >> limits.depth; limits.depth = std::max(limits.depth, 1); }
        else if (token == "nodes")     is >> limits.nodes;
        else if (token == "movetime")  { is >> limits.movetime; limits.movetime = std::max<TimePoint>(limits.movetime, 1); }
        else if (token == "mate")      is >> limits.mate;
        else if (token == "perft")     is >> limits.perft;
        else if (token == "infinite")  limits.infinite = true;
        else if (token == "ponder")    ponderMode = true;
    }

    if (limits.perft) {
        TimePoint t0 = now();
        u64 n = perft_nodes(pos, limits.perft);
        TimePoint dt = std::max<TimePoint>(1, now() - t0);
        sync_cout << "\nNodes searched: " << n << "  (" << dt << " ms, "
                  << n * 1000 / u64(dt) << " nps)" << sync_endl;
        return;
    }

    Threads.start_thinking(pos, nullptr, limits, ponderMode);
}

void setoption_cmd(std::istringstream& is) {
    std::string token, name, value;
    is >> token;
    while (is >> token && token != "value") name += (name.empty() ? "" : " ") + token;
    while (is >> token) value += (value.empty() ? "" : " ") + token;
    Options::set(name, value);
}

}

namespace Options {

int  hash_mb()        { return g_opt.hash; }
int  threads()        { return g_opt.threads; }
int  multi_pv()       { return g_opt.multiPV; }
int  move_overhead()  { return g_opt.moveOverhead; }
bool chess960()       { return g_opt.chess960; }
bool ponder_enabled() { return g_opt.ponder; }
bool show_wdl()       { return g_opt.showWDL; }

bool load_network(const std::string& path) {
    if (nnue::load(path)) return true;
    const std::string& dir = binary_directory();
    return !dir.empty() && path.find_first_of("/\\") == std::string::npos
        && nnue::load(dir + path);
}

void init() {
    Threads.set(size_t(g_opt.threads));
    TT.resize(size_t(g_opt.hash), g_opt.threads);
    if (load_network(g_opt.evalFile))
        sync_cout << "info string NNUE network loaded: " << nnue::net_name() << sync_endl;
    else
        sync_cout << "info string Could not load the embedded network, using the hand-crafted evaluation"
                  << sync_endl;
}

void set(const std::string& name, const std::string& value) {
    Threads.stop = true;
    Threads.main()->wait_for_search_finished();

    auto num = [&](int lo, int hi, int def) {
        char* end = nullptr;
        long v = std::strtol(value.c_str(), &end, 10);
        if (end == value.c_str()) return def;
        return int(std::clamp<long>(v, lo, hi));
    };

    if (name == "Hash") {
        g_opt.hash = num(1, 1024 * 1024, 256);
        TT.resize(size_t(g_opt.hash), g_opt.threads);
    } else if (name == "Threads") {
        g_opt.threads = num(1, 1024, 1);
        Threads.set(size_t(g_opt.threads));
        TT.resize(size_t(g_opt.hash), g_opt.threads);
    } else if (name == "MultiPV") {
        g_opt.multiPV = num(1, 256, 1);
    } else if (name == "Move Overhead") {
        g_opt.moveOverhead = num(0, 5000, 30);
    } else if (name == "UCI_Chess960") {
        g_opt.chess960 = (value == "true");
    } else if (name == "Ponder") {
        g_opt.ponder = (value == "true");
    } else if (name == "UCI_ShowWDL") {
        g_opt.showWDL = (value == "true");
    } else if (name == "EvalFile") {
        g_opt.evalFile = value;
        if (value.empty() || value == "none") {
            nnue::unload();
            sync_cout << "info string NNUE disabled, using the hand-crafted evaluation"
                      << sync_endl;
        } else if (load_network(value)) {
            sync_cout << "info string NNUE network loaded: " << nnue::net_name() << sync_endl;
        } else {
            sync_cout << "info string Could not load network " << value << sync_endl;
        }
    } else if (name == "Clear Hash") {
        Search::clear();
    }
}

void print_all() {
    sync_cout
        << "option name Hash type spin default 256 min 1 max 1048576\n"
        << "option name Threads type spin default 1 min 1 max 1024\n"
        << "option name MultiPV type spin default 1 min 1 max 256\n"
        << "option name Move Overhead type spin default 30 min 0 max 5000\n"
        << "option name Ponder type check default false\n"
        << "option name UCI_Chess960 type check default false\n"
        << "option name UCI_ShowWDL type check default false\n"
        << "option name EvalFile type string default " << nnue::EmbeddedName << "\n"
        << "option name Clear Hash type button" << sync_endl;
}

}

namespace UCI {

std::string value(Value v) {
    assert(-VALUE_INFINITE < v && v < VALUE_INFINITE);
    std::ostringstream ss;
    if (std::abs(v) < VALUE_MATE_IN_MAX_PLY)
        ss << "cp " << v / EVAL_SCALE;
    else
        ss << "mate " << (v > 0 ? VALUE_MATE - v + 1 : -VALUE_MATE - v) / 2;
    return ss.str();
}

std::string wdl(Value v, const Position& pos) {
    int mat = std::clamp(pos.count_all(), 4, 32);
    double m = mat / 32.0;
    double a = 320.0 - 100.0 * m, b = 60.0 + 40.0 * m;
    int win  = int(1000.0 / (1.0 + std::exp((a - double(v)) / b)));
    int loss = int(1000.0 / (1.0 + std::exp((a + double(v)) / b)));
    std::ostringstream ss;
    ss << " wdl " << win << " " << std::max(0, 1000 - win - loss) << " " << loss;
    return ss.str();
}

std::string move(Move m, bool chess960) { return move_to_uci(m, chess960); }

Move to_move(const Position& pos, std::string str) {
    if (str.length() == 5) str[4] = char(tolower(str[4]));
    for (const auto& m : MoveList<LEGAL>(pos))
        if (str == move_to_uci(m.move, pos.is_chess960())) return m.move;

    for (const auto& m : MoveList<LEGAL>(pos))
        if (str == move_to_uci(m.move, false)) return m.move;
    return Move::none();
}

std::string pv(const Thread& th, int depth) {
    std::ostringstream ss;
    TimePoint elapsed = Time.elapsed() + 1;
    u64 nodes = Threads.nodes_searched();
    size_t multiPV = std::min<size_t>(size_t(Options::multi_pv()), th.rootMoves.size());

    for (size_t i = 0; i < multiPV; ++i) {
        bool updated = th.rootMoves[i].score != -VALUE_INFINITE;
        if (depth == 1 && !updated && i > 0) continue;

        int d = updated ? depth : std::max(1, depth - 1);
        Value v = updated ? th.rootMoves[i].uciScore : th.rootMoves[i].previousScore;
        if (v == -VALUE_INFINITE) v = VALUE_ZERO;

        if (ss.tellp()) ss << "\n";
        ss << "info depth " << d
           << " seldepth " << th.rootMoves[i].selDepth
           << " multipv " << i + 1
           << " score " << UCI::value(v);

        if (Options::show_wdl()) ss << UCI::wdl(v, th.rootPos);
        if (i == th.pvIdx && updated && depth > 1) {
            if (th.rootMoves[i].scoreLowerbound) ss << " lowerbound";
            else if (th.rootMoves[i].scoreUpperbound) ss << " upperbound";
        }

        ss << " nodes " << nodes
           << " nps " << nodes * 1000 / u64(elapsed)
           << " hashfull " << TT.hashfull()
           << " time " << elapsed
           << " pv";
        for (Move m : th.rootMoves[i].pv) ss << " " << UCI::move(m, th.rootPos.is_chess960());
    }
    return ss.str();
}

void loop(int argc, char** argv) {
    Position pos;
    g_states.clear();
    g_states.emplace_back();
    pos.set(StartFEN, false, &g_states.back());

    std::string cmd, token;
    for (int i = 1; i < argc; ++i) cmd += std::string(argv[i]) + " ";

    do {
        if (argc == 1 && !std::getline(std::cin, cmd)) cmd = "quit";

        std::istringstream is(cmd);
        token.clear();
        is >> std::skipws >> token;

        if (token == "quit" || token == "stop") {
            Threads.stop = true;
        } else if (token == "ponderhit") {
            Threads.main()->ponder = false;
        } else if (token == "uci") {
            sync_cout << "id name " << engine_info() << "\nid author Blitz contributors"
                      << sync_endl;
            Options::print_all();
            sync_cout << "uciok" << sync_endl;
        } else if (token == "setoption") {
            setoption_cmd(is);
        } else if (token == "go") {
            go_cmd(pos, is);
        } else if (token == "position") {
            position_cmd(pos, is);
        } else if (token == "ucinewgame") {
            Search::clear();
        } else if (token == "isready") {
            sync_cout << "readyok" << sync_endl;
        } else if (token == "d") {
            sync_cout << pos << sync_endl;
        } else if (token == "eval") {
            sync_cout << Eval::trace(pos) << sync_endl;
        } else if (token == "bench") {
            benchmark(pos, is);
        } else if (token == "nnuecheck") {
            int games = 200;
            if (is >> token) {
                char* end = nullptr;
                long g = std::strtol(token.c_str(), &end, 10);
                if (end != token.c_str() && g > 0) games = int(g);
            }
            nnue_check(games, 240, 0x5EEDULL);
        } else if (!token.empty()) {
            sync_cout << "Unknown command: '" << cmd << "'" << sync_endl;
        }
    } while (token != "quit" && argc == 1);

    Threads.main()->wait_for_search_finished();
}

}
}
