#include "thread.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"
#include "uci.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <utility>
#include <vector>

namespace blitz {

ThreadPool Threads;
thread_local Thread* thisThread = nullptr;

Thread::Thread(size_t id) : idx_(id), stdThread_(&Thread::idle_loop, this) {
    wait_for_search_finished();
}

Thread::~Thread() {
    assert(!searching);
    exit_ = true;
    start_searching();
    stdThread_.join();
}

void Thread::clear() {
    mainHistory.fill(0);
    captureHistory.fill(0);
    pawnHistory.fill(0);
    pawnCorrectionHistory.fill(0);
    materialCorrectionHistory.fill(0);

    for (bool inCheck : { false, true })
        for (bool capture : { false, true })
            for (auto& pieceRow : continuationHistory[inCheck][capture])
                for (auto& h : pieceRow) h.fill(-427);
}

void Thread::start_searching() {
    { std::lock_guard lk(mutex_); searching = true; }
    cv_.notify_all();
}

void Thread::wait_for_search_finished() {
    std::unique_lock lk(mutex_);
    cv_.wait(lk, [&] { return !searching; });
}

void Thread::idle_loop() {
    thisThread = this;
    while (true) {
        std::unique_lock lk(mutex_);
        searching = false;
        cv_.notify_all();
        cv_.wait(lk, [&] { return bool(searching); });
        if (exit_) return;
        lk.unlock();

        search();
    }
}

void ThreadPool::set(size_t n) {
    if (!threads_.empty()) {
        main()->wait_for_search_finished();
        while (!threads_.empty()) { delete threads_.back(); threads_.pop_back(); }
    }
    if (n == 0) return;

    threads_.push_back(new MainThread(0));
    for (size_t i = 1; i < n; ++i) threads_.push_back(new Thread(i));
    clear();
}

void ThreadPool::clear() {
    for (Thread* th : threads_) th->clear();
    main()->callsCnt = 0;
    main()->bestPreviousScore = VALUE_INFINITE;
    main()->bestPreviousAverageScore = VALUE_INFINITE;
    main()->previousTimeReduction = 1.0;
}

void ThreadPool::start_thinking(Position& pos, StateInfo* setupStates,
                                const Search::LimitsType& limits, bool ponderMode) {
    stop = true;
    main()->wait_for_search_finished();

    main()->stopOnPonderhit = stop = false;
    main()->ponder = ponderMode;
    Search::Limits = limits;

    Search::RootMoves rootMoves;
    for (const auto& m : MoveList<LEGAL>(pos))
        if (limits.searchmoves.empty()
            || std::count(limits.searchmoves.begin(), limits.searchmoves.end(), m.move))
            rootMoves.emplace_back(m.move);

    for (Thread* th : threads_) {
        th->nodes = 0;
        th->rootDepth = th->completedDepth = 0;
        th->selDepth = 0;
        th->rootMoves = rootMoves;
        th->rootPos.set(pos.fen(), pos.is_chess960(), &th->rootState);
        th->rootState = *pos.state();
    }
    (void)setupStates;

    for (Thread* th : threads_) th->start_searching();
}

u64 ThreadPool::nodes_searched() const {
    u64 sum = 0;
    for (Thread* th : threads_) sum += th->nodes.load(std::memory_order_relaxed);
    return sum;
}

void ThreadPool::wait_for_search_finished() const {
    for (Thread* th : threads_)
        if (th != threads_.front()) th->wait_for_search_finished();
}

Thread* ThreadPool::best_thread() const {
    Thread* best = threads_.front();
    Value minScore = VALUE_NONE;

    for (Thread* th : threads_)
        minScore = std::min(minScore, th->rootMoves[0].score);

    std::vector<std::pair<u16, i64>> votes;
    auto vote_for = [&votes](Move m) -> i64& {
        for (auto& v : votes)
            if (v.first == m.raw()) return v.second;
        votes.emplace_back(m.raw(), i64(0));
        return votes.back().second;
    };

    for (Thread* th : threads_)
        vote_for(th->rootMoves[0].pv[0]) +=
            i64(th->rootMoves[0].score - minScore + 14) * th->completedDepth;

    for (Thread* th : threads_) {
        const Value bs = best->rootMoves[0].score, ts = th->rootMoves[0].score;
        if (std::abs(bs) >= VALUE_TB_WIN_IN_MAX_PLY) {

            if (ts > bs) best = th;
        } else if (ts >= VALUE_TB_WIN_IN_MAX_PLY
                   || (ts > VALUE_MATED_IN_MAX_PLY
                       && vote_for(th->rootMoves[0].pv[0]) > vote_for(best->rootMoves[0].pv[0]))) {
            best = th;
        }
    }
    return best;
}

}
