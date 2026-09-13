#pragma once
#include "misc.h"
#include "movepick.h"
#include "position.h"
#include "types.h"
#include <atomic>
#include <vector>

namespace blitz {

class Thread;

namespace Search {

struct Stack {
    Move*  pv;
    PieceToHistory* continuationHistory;
    int    ply;
    Move   currentMove;
    Move   excludedMove;
    Move   killers[2];
    Value  staticEval;
    int    statScore;
    int    moveCount;
    bool   inCheck;
    bool   ttPv;
    bool   ttHit;
    int    cutoffCnt;
    int    doubleExtensions;
};

struct RootMove {
    explicit RootMove(Move m) : pv(1, m) {}
    bool operator==(const Move& m) const { return pv[0] == m; }

    bool operator<(const RootMove& m) const {
        return m.score != score ? m.score < score : m.previousScore < previousScore;
    }

    Value score         = -VALUE_INFINITE;
    Value previousScore = -VALUE_INFINITE;
    Value averageScore  = -VALUE_INFINITE;
    Value uciScore      = -VALUE_INFINITE;
    bool  scoreLowerbound = false;
    bool  scoreUpperbound = false;
    int   selDepth = 0;
    u64   effort = 0;
    std::vector<Move> pv;
};

using RootMoves = std::vector<RootMove>;

struct LimitsType {
    LimitsType() { time[WHITE] = time[BLACK] = inc[WHITE] = inc[BLACK] = TimePoint(0); }
    bool use_time_management() const { return time[WHITE] || time[BLACK]; }

    std::vector<Move> searchmoves;
    TimePoint time[COLOR_NB], inc[COLOR_NB], movetime = 0, startTime = 0;
    int   movestogo = 0, depth = 0, mate = 0, perft = 0;
    bool  infinite = false;
    u64   nodes = 0;
};

extern LimitsType Limits;

extern std::atomic<bool> Silent;

void init();
void clear();

}
}
