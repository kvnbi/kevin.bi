#pragma once
#include "history.h"
#include "position.h"
#include "search.h"
#include "thread_stack.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <thread>
#include <memory>
#include <vector>

namespace blitz {

class Thread {
public:
    explicit Thread(size_t id);
    virtual ~Thread();

    virtual void search();
    void clear();
    void idle_loop();
    void start_searching();
    void wait_for_search_finished();
    size_t id() const { return idx_; }
    bool is_main() const { return idx_ == 0; }

    Position   rootPos;
    StateInfo  rootState;
    Search::RootMoves rootMoves;
    int  rootDepth = 0, completedDepth = 0, selDepth = 0;
    int  nmpMinPly = 0;
    std::atomic<u64> nodes{0};
    std::atomic<bool> searching{true};
    size_t pvIdx = 0, pvLast = 0;
    Value  rootDelta = VALUE_ZERO;

    ButterflyHistory      mainHistory;
    CapturePieceToHistory captureHistory;
    ContinuationHistory   continuationHistory[2][2];
    PawnHistory           pawnHistory;
    CorrectionHistory     pawnCorrectionHistory;
    CorrectionHistory     materialCorrectionHistory;

protected:
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t idx_;
    bool exit_ = false;
    NativeThread stdThread_;
};

class MainThread : public Thread {
public:
    using Thread::Thread;
    void search() override;
    void check_time();

    double previousTimeReduction = 1.0;
    Value  bestPreviousScore = VALUE_INFINITE;
    Value  bestPreviousAverageScore = VALUE_INFINITE;
    Value  iterValue[4] = {};
    int    callsCnt = 0;
    bool   stopOnPonderhit = false;
    std::atomic<bool> ponder{false};
};

class ThreadPool {
public:
    void set(size_t n);
    void clear();
    void start_thinking(Position& pos, StateInfo* setupStates,
                        const Search::LimitsType& limits, bool ponderMode = false);

    MainThread* main() const { return static_cast<MainThread*>(threads_.front()); }
    Thread* best_thread() const;
    u64 nodes_searched() const;
    void wait_for_search_finished() const;

    size_t size() const { return threads_.size(); }
    auto begin() const { return threads_.begin(); }
    auto end() const { return threads_.end(); }
    Thread* operator[](size_t i) const { return threads_[i]; }

    std::atomic<bool> stop{false};

private:
    std::vector<Thread*> threads_;
};

extern ThreadPool Threads;

extern thread_local Thread* thisThread;

}
