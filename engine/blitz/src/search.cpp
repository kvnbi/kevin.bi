#include "search.h"
#include "evaluate.h"
#include "history.h"
#include "movegen.h"
#include "movepick.h"
#include "nnue/network.h"
#include "thread.h"
#include "timeman.h"
#include "tt.h"
#include "uci.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace blitz {

namespace Search { LimitsType Limits; std::atomic<bool> Silent{false}; }

using namespace Search;

namespace {

enum NodeType { NonPV, PV, Root };

int Reductions[MAX_MOVES];

int reduction(bool improving, int depth, int moveNumber, int delta, int rootDelta) {
    int r = Reductions[depth] * Reductions[moveNumber];
    return r - delta * 735 / std::max(rootDelta, 1) + (!improving) * r * 191 / 512;
}

constexpr int futility_move_count(bool improving, int depth) {
    return improving ? 3 + depth * depth : (3 + depth * depth) / 2;
}

Value value_draw(const Thread* th) {
    return VALUE_DRAW - 1 + Value(th->nodes.load(std::memory_order_relaxed) & 0x2);
}

Value to_corrected_static_eval(Value v, const Thread* th, const Position& pos) {
    Color us = pos.side_to_move();
    int pcv = th->pawnCorrectionHistory[us][correction_index(pos.pawn_key())];
    int mcv = th->materialCorrectionHistory[us][correction_index(pos.material_key())];
    int cv  = (5932 * pcv + 2994 * mcv) / 16384;
    v += cv / 32;
    return std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
}

void update_pv(Move* pv, Move move, const Move* childPv) {
    for (*pv++ = move; childPv && *childPv != Move::none();) *pv++ = *childPv++;
    *pv = Move::none();
}

void update_continuation_histories(Stack* ss, Piece pc, Square to, int bonus) {

    for (int i : { 1, 2, 3, 4, 6 })
        if (ss->inCheck && i > 2) break;
        else if ((ss - i)->currentMove != Move::none())
            (*(ss - i)->continuationHistory)[pc][to] << bonus / (1 + 3 * (i == 3));
}

void update_quiet_stats(const Position& pos, Stack* ss, Thread* th, Move move, int bonus) {

    if (ss->killers[0] != move) {
        ss->killers[1] = ss->killers[0];
        ss->killers[0] = move;
    }

    Color us = pos.side_to_move();
    th->mainHistory[us][move.from_to()] << bonus;
    th->pawnHistory[pawn_structure_index(pos.pawn_key())][pos.moved_piece(move)][move.to()] << bonus;
    update_continuation_histories(ss, pos.moved_piece(move), move.to(), bonus);
}

void update_all_stats(const Position& pos, Stack* ss, Thread* th, Move bestMove, Value bestValue,
                      Value beta, Square prevSq, Move* quietsSearched, int quietCount,
                      Move* capturesSearched, int captureCount, int depth) {
    Piece moved = pos.moved_piece(bestMove);
    PieceType captured;

    int quietBonus = stat_bonus(depth + 1);
    int quietMalus = stat_malus(depth);

    if (!pos.capture_stage(bestMove)) {
        update_quiet_stats(pos, ss, th, bestMove, quietBonus);
        for (int i = 0; i < quietCount; ++i) {
            th->mainHistory[pos.side_to_move()][quietsSearched[i].from_to()] << -quietMalus;
            th->pawnHistory[pawn_structure_index(pos.pawn_key())]
                           [pos.moved_piece(quietsSearched[i])][quietsSearched[i].to()] << -quietMalus;
            update_continuation_histories(ss, pos.moved_piece(quietsSearched[i]),
                                          quietsSearched[i].to(), -quietMalus);
        }
    } else {
        captured = type_of(pos.piece_on(bestMove.to()));
        th->captureHistory[moved][bestMove.to()][captured] << quietBonus;
    }

    if (prevSq != SQ_NONE && ((ss - 1)->moveCount == 1 + (ss - 1)->ttHit)
        && !pos.captured_piece())
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq, -quietMalus);

    for (int i = 0; i < captureCount; ++i) {
        moved = pos.moved_piece(capturesSearched[i]);
        captured = type_of(pos.piece_on(capturesSearched[i].to()));
        th->captureHistory[moved][capturesSearched[i].to()][captured] << -quietMalus;
    }
    (void)bestValue; (void)beta;
}

template <NodeType nodeType>
Value search(Position& pos, Stack* ss, Value alpha, Value beta, int depth, bool cutNode);

template <NodeType nodeType>
Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, int depth = 0);

}

void Search::init() {
    for (int i = 1; i < MAX_MOVES; ++i)
        Reductions[i] = int(20.57 * std::log(i));
}

void Search::clear() {
    Threads.stop = true;
    Threads.main()->wait_for_search_finished();
    Threads.wait_for_search_finished();
    TT.clear(int(Threads.size()));
    Threads.clear();
}

void MainThread::search() {
    Color us = rootPos.side_to_move();
    Time.init(Limits, us, rootPos.game_ply());
    TT.new_search();

    if (rootMoves.empty()) {
        rootMoves.emplace_back(Move::none());
        if (!Search::Silent)
            sync_cout << "info depth 0 score "
                      << UCI::value(rootPos.checkers() ? -VALUE_MATE : VALUE_DRAW) << sync_endl;
    } else {
        for (Thread* th : Threads)
            if (th != this) th->start_searching();
        Thread::search();
    }

    while (!Threads.stop && (ponder || Limits.infinite))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    Threads.stop = true;
    Threads.wait_for_search_finished();

    Thread* best = this;
    if (Threads.size() > 1 && !Limits.depth && rootMoves[0].pv[0] != Move::none())
        best = Threads.best_thread();

    bestPreviousScore = best->rootMoves[0].score;
    bestPreviousAverageScore = best->rootMoves[0].averageScore;

    if (Search::Silent) return;

    if (best != this)
        sync_cout << UCI::pv(*best, best->completedDepth) << sync_endl;

    std::string out = "bestmove " + UCI::move(best->rootMoves[0].pv[0], rootPos.is_chess960());
    if (best->rootMoves[0].pv.size() > 1)
        out += " ponder " + UCI::move(best->rootMoves[0].pv[1], rootPos.is_chess960());
    sync_cout << out << sync_endl;
}

void Thread::search() {
    Stack stack[MAX_PLY + 10] = {};
    Stack* ss = stack + 7;
    Move pv[MAX_PLY + 1];
    Value bestValue = -VALUE_INFINITE, alpha = -VALUE_INFINITE, beta = VALUE_INFINITE;
    Value lastBestScore = -VALUE_INFINITE;
    Move  lastBestMove = Move::none();
    int   lastBestDepth = 0;
    double timeReduction = 1.0, totBestMoveChanges = 0;

    for (int i = 7; i > 0; --i) {
        (ss - i)->continuationHistory = &continuationHistory[0][0][NO_PIECE][0];
        (ss - i)->staticEval = VALUE_NONE;
        (ss - i)->currentMove = Move::none();
    }
    for (int i = 0; i <= MAX_PLY + 2; ++i) ss[i].ply = i;
    ss->pv = pv;

    MainThread* mainThread = is_main() ? static_cast<MainThread*>(this) : nullptr;
    int multiPV = std::min<size_t>(Options::multi_pv(), rootMoves.size());

    if (nnue::available()) nnue::refresh_accumulator(rootPos, rootPos.state());

    while (++rootDepth < MAX_PLY && (rootDepth == 1 || !Threads.stop)
           && !(Limits.depth && mainThread && rootDepth > Limits.depth)) {

        if (!mainThread) {
            int i = int((id() - 1) % 20);
            static const int skipSize[20] = { 1,1,2,2,2,3,3,3,4,4,4,5,5,5,6,6,6,7,7,7 };
            static const int skipPhase[20] = { 0,1,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2 };
            if (((rootDepth + rootPos.game_ply()) / skipSize[i]) % 2 != skipPhase[i] % 2)
                continue;
        }

        for (RootMove& rm : rootMoves) rm.previousScore = rm.score;
        size_t pvFirst = 0;

        for (pvIdx = 0; pvIdx < size_t(multiPV) && (rootDepth == 1 || !Threads.stop); ++pvIdx) {
            selDepth = 0;

            Value avg = rootMoves[pvIdx].averageScore;
            if (avg == -VALUE_INFINITE) avg = VALUE_ZERO;
            int   delta = 9 + avg * avg / 12800;
            if (rootDepth >= 4) {
                alpha = std::max(avg - delta, -VALUE_INFINITE);
                beta  = std::min(avg + delta,  VALUE_INFINITE);
            } else {
                alpha = -VALUE_INFINITE;
                beta = VALUE_INFINITE;
            }

            int failedHighCnt = 0;
            while (true) {
                rootDelta = beta - alpha;
                int adjustedDepth = std::max(1, rootDepth - failedHighCnt);
                bestValue = ::blitz::search<Root>(rootPos, ss, alpha, beta, adjustedDepth, false);

                std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIdx + 1);
                std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.end());

                if (Threads.stop && rootDepth > 1) break;

                if (mainThread && !Search::Silent && multiPV == 1
                    && (bestValue <= alpha || bestValue >= beta) && Time.elapsed() > 3000)
                    sync_cout << UCI::pv(*this, rootDepth) << sync_endl;

                if (bestValue <= alpha) {
                    beta = (alpha + beta) / 2;
                    alpha = std::max(bestValue - delta, -VALUE_INFINITE);
                    failedHighCnt = 0;
                    if (mainThread) mainThread->stopOnPonderhit = false;
                } else if (bestValue >= beta) {
                    beta = std::min(bestValue + delta, VALUE_INFINITE);
                    ++failedHighCnt;
                } else {
                    break;
                }
                delta += delta / 3;
            }

            std::stable_sort(rootMoves.begin() + pvFirst, rootMoves.begin() + pvIdx + 1);

            if (mainThread && !Search::Silent
                && (Threads.stop || pvIdx + 1 == size_t(multiPV) || Time.elapsed() > 3000))
                sync_cout << UCI::pv(*this, rootDepth) << sync_endl;
        }

        if (!Threads.stop || rootDepth == 1) completedDepth = rootDepth;

        if (rootMoves[0].pv[0] != lastBestMove) {
            lastBestMove = rootMoves[0].pv[0];
            lastBestScore = rootMoves[0].score;
            lastBestDepth = rootDepth;
        }

        if (!mainThread) continue;

        if (Limits.mate && rootMoves[0].score == rootMoves[0].uciScore
            && ((rootMoves[0].score >= VALUE_MATE_IN_MAX_PLY
                 && VALUE_MATE - rootMoves[0].score <= 2 * Limits.mate)
                || (rootMoves[0].score != -VALUE_INFINITE
                    && rootMoves[0].score <= VALUE_MATED_IN_MAX_PLY
                    && VALUE_MATE + rootMoves[0].score <= 2 * Limits.mate)))
            Threads.stop = true;

        if (Limits.use_time_management() && !Threads.stop && !mainThread->stopOnPonderhit) {

            double fallingEval = (11 + 2 * (mainThread->bestPreviousAverageScore - bestValue)
                                  + (mainThread->iterValue[(rootDepth - 1) % 4] - bestValue)) / 100.0;
            fallingEval = std::clamp(fallingEval, 0.58, 1.5);

            timeReduction = (lastBestDepth + 9 < completedDepth) ? 1.68 : 0.79;
            double reduction = (1.47 + mainThread->previousTimeReduction) / (2.32 * timeReduction);
            double bestMoveInstability = 1.0 + 1.9 * totBestMoveChanges / std::max<size_t>(1, Threads.size());

            double total = Time.optimum() * fallingEval * reduction * bestMoveInstability;

            if (Time.elapsed() > total) {
                if (mainThread->ponder) mainThread->stopOnPonderhit = true;
                else Threads.stop = true;
            }
            totBestMoveChanges /= 2;
        }

        mainThread->iterValue[rootDepth % 4] = bestValue;
    }

    if (mainThread) mainThread->previousTimeReduction = timeReduction;
    (void)lastBestScore;
}

namespace {

template <NodeType nodeType>
Value search(Position& pos, Stack* ss, Value alpha, Value beta, int depth, bool cutNode) {
    constexpr bool PvNode   = nodeType != NonPV;
    constexpr bool rootNode = nodeType == Root;

    if (depth <= 0) return qsearch<PvNode ? PV : NonPV>(pos, ss, alpha, beta);

    assert(-VALUE_INFINITE <= alpha && alpha < beta && beta <= VALUE_INFINITE);
    assert(!(PvNode && cutNode));

    Thread* th = thisThread;
    Move pv[MAX_PLY + 1], capturesSearched[32], quietsSearched[64];
    StateInfo st;

    Key   posKey;
    Move  ttMove, move, excludedMove, bestMove;
    int   extension, newDepth;
    Value bestValue, value, ttValue, eval, maxValue, probCutBeta;
    bool  givesCheck, improving, priorCapture, opponentWorsening;
    bool  capture, ttCapture;
    int   moveCount, captureCount, quietCount;

    ss->inCheck = bool(pos.checkers());
    priorCapture = bool(pos.captured_piece());
    Color us = pos.side_to_move();
    moveCount = captureCount = quietCount = ss->moveCount = 0;
    bestValue = -VALUE_INFINITE;
    maxValue = VALUE_INFINITE;

    if (th->is_main()) static_cast<MainThread*>(th)->check_time();
    if (PvNode && th->selDepth < ss->ply + 1) th->selDepth = ss->ply + 1;

    if (!rootNode) {

        if (pos.upcoming_repetition(ss->ply)) {
            alpha = value_draw(th);
            if (alpha >= beta) return alpha;
        }

        if (Threads.stop.load(std::memory_order_relaxed) || pos.is_draw(ss->ply)
            || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !ss->inCheck) ? Eval::evaluate(pos) : value_draw(th);

        alpha = std::max(mated_in(ss->ply), alpha);
        beta  = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta) return alpha;
    }

    bestMove = Move::none();
    (ss + 1)->excludedMove = Move::none();
    (ss + 2)->cutoffCnt = 0;
    (ss + 2)->killers[0] = (ss + 2)->killers[1] = Move::none();
    ss->doubleExtensions = (ss - 1)->doubleExtensions;
    Square prevSq = (ss - 1)->currentMove != Move::none() ? (ss - 1)->currentMove.to() : SQ_NONE;

    excludedMove = ss->excludedMove;
    posKey = pos.key();
    auto [ttHit, tte] = TT.probe(posKey);
    ss->ttHit = ttHit;
    ttValue = ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    ttMove = rootNode ? th->rootMoves[th->pvIdx].pv[0] : ttHit ? tte->move() : Move::none();
    ttCapture = ttMove && pos.capture_stage(ttMove);
    if (!excludedMove) ss->ttPv = PvNode || (ttHit && tte->is_pv());

    if (!PvNode && !excludedMove && ttHit && tte->depth() > depth - (ttValue <= beta)
        && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))) {

        if (ttMove && ttValue >= beta) {
            if (!ttCapture) update_quiet_stats(pos, ss, th, ttMove, stat_bonus(depth));

            if (prevSq != SQ_NONE && (ss - 1)->moveCount <= 2 && !priorCapture)
                update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                              -stat_malus(depth + 1));
        }

        if (pos.rule50_count() < 90) return ttValue;
    }

    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck) {
        ss->staticEval = eval = VALUE_NONE;
        improving = opponentWorsening = false;
        goto moves_loop;
    } else if (excludedMove) {
        unadjustedStaticEval = eval = ss->staticEval;
    } else if (ttHit) {
        unadjustedStaticEval = tte->eval();
        if (unadjustedStaticEval == VALUE_NONE) unadjustedStaticEval = Eval::evaluate(pos);
        ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, th, pos);

        if (ttValue != VALUE_NONE
            && (tte->bound() & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
            eval = ttValue;
    } else {
        unadjustedStaticEval = Eval::evaluate(pos);
        ss->staticEval = eval = to_corrected_static_eval(unadjustedStaticEval, th, pos);
        tte->save(posKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_NONE, Move::none(),
                  unadjustedStaticEval, TT.generation());
    }

    improving = (ss - 2)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 2)->staticEval
              : (ss - 4)->staticEval != VALUE_NONE ? ss->staticEval > (ss - 4)->staticEval
                                                   : true;
    opponentWorsening = ss->staticEval + (ss - 1)->staticEval > 2;

    if (!PvNode && eval < alpha - 462 - 297 * depth * depth) {
        value = qsearch<NonPV>(pos, ss, alpha - 1, alpha);
        if (value < alpha) return value;
    }

    if (!ss->ttPv && depth < 12
        && eval - (89 * depth - 22 * improving - 15 * opponentWorsening) >= beta
        && eval >= beta && eval < 27000 && (!ttMove || ttCapture))
        return beta + (eval - beta) / 3;

    if (!PvNode && (ss - 1)->currentMove != Move::null() && eval >= beta
        && ss->staticEval >= beta - 21 * depth + 421 && !excludedMove
        && pos.has_non_pawn_material(us) && ss->ply >= th->nmpMinPly
        && beta > VALUE_MATED_IN_MAX_PLY) {

        int R = std::min(int(eval - beta) / 202, 6) + depth / 3 + 4;

        ss->currentMove = Move::null();
        ss->continuationHistory = &th->continuationHistory[0][0][NO_PIECE][0];

        pos.do_null_move(st);
        Value nullValue = -search<NonPV>(pos, ss + 1, -beta, -beta + 1, depth - R, !cutNode);
        pos.undo_null_move();

        if (nullValue >= beta && nullValue < VALUE_MATE_IN_MAX_PLY) {
            if (th->nmpMinPly || depth < 16) return nullValue;

            th->nmpMinPly = ss->ply + 3 * (depth - R) / 4;
            Value v = search<NonPV>(pos, ss, beta - 1, beta, depth - R, false);
            th->nmpMinPly = 0;
            if (v >= beta) return nullValue;
        }
    }

    if (PvNode && !ttMove) depth -= 3;
    if (depth <= 0) return qsearch<PV>(pos, ss, alpha, beta);
    if (cutNode && depth >= 8 && !ttMove) depth -= 2;

    probCutBeta = beta + 184 - 53 * improving;
    if (!PvNode && depth > 3 && std::abs(beta) < VALUE_MATE_IN_MAX_PLY
        && !(ttHit && tte->depth() >= depth - 3 && ttValue != VALUE_NONE && ttValue < probCutBeta)) {

        MovePicker mp(pos, ttMove, probCutBeta - ss->staticEval, &th->captureHistory);
        while ((move = mp.next_move()) != Move::none()) {
            if (move == excludedMove || !pos.legal(move)) continue;

            ss->currentMove = move;
            ss->continuationHistory =
                &th->continuationHistory[ss->inCheck][true][pos.moved_piece(move)][move.to()];

            pos.do_move(move, st);
            value = -qsearch<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1);
            if (value >= probCutBeta)
                value = -search<NonPV>(pos, ss + 1, -probCutBeta, -probCutBeta + 1,
                                       depth - 4, !cutNode);
            pos.undo_move(move);

            if (value >= probCutBeta) {
                tte->save(posKey, value_to_tt(value, ss->ply), ss->ttPv, BOUND_LOWER,
                          depth - 3, move, unadjustedStaticEval, TT.generation());
                return value;
            }
        }
    }

moves_loop:

    {
    const PieceToHistory* contHist[] = {
        (ss - 1)->continuationHistory, (ss - 2)->continuationHistory,
        (ss - 3)->continuationHistory, (ss - 4)->continuationHistory,
        (ss - 5)->continuationHistory, (ss - 6)->continuationHistory
    };

    MovePicker mp(pos, ttMove, depth, &th->mainHistory, &th->captureHistory,
                  contHist, &th->pawnHistory, ss->killers);

    value = bestValue;
    bool moveCountPruning = false;

    while ((move = mp.next_move(moveCountPruning)) != Move::none()) {
        if (move == excludedMove) continue;
        if (rootNode && !std::count(th->rootMoves.begin() + th->pvIdx,
                                    th->rootMoves.end(), move))
            continue;
        if (!rootNode && !pos.legal(move)) continue;

        ss->moveCount = ++moveCount;

        if (rootNode && th->is_main() && !Search::Silent && Time.elapsed() > 3000)
            sync_cout << "info depth " << depth << " currmove "
                      << UCI::move(move, pos.is_chess960())
                      << " currmovenumber " << moveCount + th->pvIdx << sync_endl;

        if (PvNode) (ss + 1)->pv = nullptr;

        capture = pos.capture_stage(move);
        Piece movedPiece = pos.moved_piece(move);
        givesCheck = pos.gives_check(move);
        newDepth = depth - 1;

        int delta = beta - alpha;
        int r = reduction(improving, depth, moveCount, delta, th->rootDelta);

        if (!rootNode && pos.has_non_pawn_material(us) && bestValue > VALUE_MATED_IN_MAX_PLY) {
            moveCountPruning = moveCount >= futility_move_count(improving, depth);

            int lmrDepth = newDepth - r / 1024;

            if (capture || givesCheck) {
                PieceType capturedType = type_of(pos.piece_on(move.to()));
                int captHist = th->captureHistory[movedPiece][move.to()][capturedType];

                if (!givesCheck && lmrDepth < 7 && !ss->inCheck) {
                    Value futilityValue = ss->staticEval + 285 + 277 * lmrDepth
                                        + PieceValue[capturedType] + captHist / 7;
                    if (futilityValue <= alpha) continue;
                }

                if (!pos.see_ge(move, -206 * depth)) continue;
            } else {
                int history = (*contHist[0])[movedPiece][move.to()]
                            + (*contHist[1])[movedPiece][move.to()]
                            + th->pawnHistory[pawn_structure_index(pos.pawn_key())]
                                             [movedPiece][move.to()];

                if (history < -3752 * depth) continue;

                history += 2 * th->mainHistory[us][move.from_to()];
                lmrDepth += history / 7838;

                if (!ss->inCheck && lmrDepth < 12
                    && ss->staticEval + 112 + 138 * lmrDepth <= alpha)
                    continue;

                lmrDepth = std::max(lmrDepth, 0);
                if (!pos.see_ge(move, -26 * lmrDepth * lmrDepth)) continue;
            }
        }

        extension = 0;
        if (ss->ply < th->rootDepth * 2) {
            if (!rootNode && move == ttMove && !excludedMove && depth >= 4
                && std::abs(ttValue) < VALUE_MATE_IN_MAX_PLY
                && (tte->bound() & BOUND_LOWER) && tte->depth() >= depth - 3) {

                Value singularBeta = ttValue - (58 + 76 * (ss->ttPv && !PvNode)) * depth / 64;
                int singularDepth = newDepth / 2;

                ss->excludedMove = move;
                value = search<NonPV>(pos, ss, singularBeta - 1, singularBeta,
                                      singularDepth, cutNode);
                ss->excludedMove = Move::none();

                if (value < singularBeta) {
                    extension = 1;

                    if (!PvNode && value < singularBeta - 17 && ss->doubleExtensions <= 8) {
                        extension = 2;
                        depth += depth < 15;
                    }
                } else if (singularBeta >= beta) {
                    return singularBeta;
                } else if (ttValue >= beta) {
                    extension = -2 - !PvNode;
                } else if (cutNode) {
                    extension = -2;
                } else if (ttValue <= value) {
                    extension = -1;
                }
            } else if (PvNode && move == ttMove && move.to() == prevSq
                       && th->captureHistory[movedPiece][move.to()]
                                            [type_of(pos.piece_on(move.to()))] > 4026) {
                extension = 1;
            }
        }

        newDepth += extension;
        ss->doubleExtensions = (ss - 1)->doubleExtensions + (extension == 2);

        ss->currentMove = move;
        ss->continuationHistory =
            &th->continuationHistory[ss->inCheck][capture][movedPiece][move.to()];
        u64 nodeCountBefore = th->nodes.load(std::memory_order_relaxed);

        TT.prefetch_entry(pos.key_after(move));
        pos.do_move(move, st, givesCheck);
        th->nodes.fetch_add(1, std::memory_order_relaxed);

        if (ss->ttPv) r -= 1024 + (ttValue > alpha) * 1024 + (tte->depth() >= depth) * 1024;
        if (cutNode) r += 2000;
        if (ttCapture && !capture) r += 1024;
        if ((ss + 1)->cutoffCnt > 3) r += 1024;
        else if (move == ttMove) r -= 2000;

        ss->statScore = 2 * th->mainHistory[us][move.from_to()]
                      + (*contHist[0])[movedPiece][move.to()]
                      + (*contHist[1])[movedPiece][move.to()]
                      + (*contHist[3])[movedPiece][move.to()] - 3817;
        r -= ss->statScore * 1024 / 14005;

        if (depth >= 2 && moveCount > 1 + rootNode) {
            int d = std::max(1, std::min(newDepth - r / 1024, newDepth + 1));
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true);

            if (value > alpha && d < newDepth) {
                const bool doDeeper = value > bestValue + 43 + 2 * newDepth;
                const bool doShallower = value < bestValue + newDepth;
                int nd = newDepth + doDeeper - doShallower;
                if (nd > d) value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha, nd, !cutNode);

                int bonus = value <= alpha ? -stat_malus(newDepth)
                          : value >= beta  ?  stat_bonus(newDepth)
                                           :  0;
                update_continuation_histories(ss, movedPiece, move.to(), bonus);
            }
        } else if (!PvNode || moveCount > 1) {
            if (!ttMove && cutNode) r += 2 * 1024;
            value = -search<NonPV>(pos, ss + 1, -(alpha + 1), -alpha,
                                   newDepth - (r > 3500), !cutNode);
        }

        if (PvNode && (moveCount == 1 || value > alpha)) {
            pv[0] = Move::none();
            (ss + 1)->pv = pv;
            value = -search<PV>(pos, ss + 1, -beta, -alpha, newDepth, false);
        }

        pos.undo_move(move);
        assert(value > -VALUE_INFINITE && value < VALUE_INFINITE);

        if (Threads.stop.load(std::memory_order_relaxed) && (!rootNode || th->rootDepth > 1))
            return VALUE_ZERO;

        if (rootNode) {
            RootMove& rm = *std::find(th->rootMoves.begin(), th->rootMoves.end(), move);
            rm.effort += th->nodes.load(std::memory_order_relaxed) - nodeCountBefore;
            rm.averageScore = rm.averageScore != -VALUE_INFINITE
                            ? (2 * value + rm.averageScore) / 3 : value;

            if (moveCount == 1 || value > alpha) {
                rm.score = rm.uciScore = value;
                rm.selDepth = th->selDepth;
                rm.scoreLowerbound = rm.scoreUpperbound = false;

                if (value >= beta) { rm.scoreLowerbound = true; rm.uciScore = beta; }
                else if (value <= alpha) { rm.scoreUpperbound = true; rm.uciScore = alpha; }

                rm.pv.resize(1);
                for (Move* m = (ss + 1)->pv; m && *m != Move::none(); ++m) rm.pv.push_back(*m);
            } else {
                rm.score = -VALUE_INFINITE;
            }
        }

        if (value > bestValue) {
            bestValue = value;
            if (value > alpha) {
                bestMove = move;
                if (PvNode && !rootNode) update_pv(ss->pv, move, (ss + 1)->pv);

                if (value >= beta) {
                    ss->cutoffCnt += 1 + !ttMove;
                    break;
                }
                alpha = value;
                if (depth > 2 && depth < 13 && std::abs(value) < 12000) depth -= 2;
                assert(depth > 0);
            }
        }

        if (move != bestMove) {
            if (capture && captureCount < 32) capturesSearched[captureCount++] = move;
            else if (!capture && quietCount < 64) quietsSearched[quietCount++] = move;
        }
    }

    assert(moveCount || !ss->inCheck || excludedMove || !MoveList<LEGAL>(pos).size());

    if (!moveCount)
        bestValue = excludedMove ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;
    else if (bestMove)
        update_all_stats(pos, ss, th, bestMove, bestValue, beta, prevSq, quietsSearched,
                         quietCount, capturesSearched, captureCount, depth);
    else if (prevSq != SQ_NONE && !priorCapture) {

        int bonus = (depth > 5) + (PvNode || cutNode) + ((ss - 1)->statScore < -14000)
                  + ((ss - 1)->moveCount > 11);
        update_continuation_histories(ss - 1, pos.piece_on(prevSq), prevSq,
                                      stat_bonus(depth) * bonus / 2);
    }

    bestValue = std::min(bestValue, maxValue);
    }

    if (!excludedMove && !(rootNode && th->pvIdx))
        tte->save(posKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                  bestValue >= beta ? BOUND_LOWER
                  : (PvNode && bestMove) ? BOUND_EXACT : BOUND_UPPER,
                  depth, bestMove, unadjustedStaticEval, TT.generation());

    if (!ss->inCheck && (!bestMove || !pos.capture_stage(bestMove))
        && !(bestValue >= beta && bestValue <= VALUE_MATED_IN_MAX_PLY)
        && !(!bestMove && bestValue >= beta)) {
        int bonus = std::clamp(int(bestValue - ss->staticEval) * depth / 8,
                               -CORRECTION_HISTORY_LIMIT / 4, CORRECTION_HISTORY_LIMIT / 4);
        th->pawnCorrectionHistory[us][correction_index(pos.pawn_key())] << bonus;
        th->materialCorrectionHistory[us][correction_index(pos.material_key())] << bonus;
    }

    assert(bestValue > -VALUE_INFINITE && bestValue < VALUE_INFINITE);
    return bestValue;
}

template <NodeType nodeType>
Value qsearch(Position& pos, Stack* ss, Value alpha, Value beta, int depth) {
    constexpr bool PvNode = nodeType == PV;
    static_assert(nodeType != Root, "qsearch is never a root node");
    assert(alpha < beta);
    assert(depth <= 0);

    Thread* th = thisThread;
    Move pv[MAX_PLY + 1];
    StateInfo st;

    if (PvNode && th->selDepth < ss->ply + 1) th->selDepth = ss->ply + 1;

    if (PvNode) {
        (ss + 1)->pv = pv;
        ss->pv = pv;
        pv[0] = Move::none();
    }

    Value oldAlpha = alpha;
    ss->inCheck = bool(pos.checkers());
    Value bestValue, value, futilityBase;
    Move  bestMove = Move::none(), move;
    int   moveCount = 0;

    if (pos.is_draw(ss->ply) || ss->ply >= MAX_PLY)
        return (ss->ply >= MAX_PLY && !ss->inCheck) ? Eval::evaluate(pos) : VALUE_DRAW;

    int ttDepth = (ss->inCheck || depth >= DEPTH_QS_CHECK) ? DEPTH_QS_CHECK : DEPTH_QS_NO_CHECK;

    Key posKey = pos.key();
    auto [ttHit, tte] = TT.probe(posKey);
    ss->ttHit = ttHit;
    Value ttValue = ttHit ? value_from_tt(tte->value(), ss->ply, pos.rule50_count()) : VALUE_NONE;
    Move  ttMove = ttHit ? tte->move() : Move::none();
    bool  pvHit = ttHit && tte->is_pv();

    if (!PvNode && ttHit && tte->depth() >= ttDepth && ttValue != VALUE_NONE
        && (tte->bound() & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    Value unadjustedStaticEval = VALUE_NONE;
    if (ss->inCheck) {
        bestValue = futilityBase = -VALUE_INFINITE;
        ss->staticEval = VALUE_NONE;
    } else {
        if (ttHit) {
            unadjustedStaticEval = tte->eval();
            if (unadjustedStaticEval == VALUE_NONE) unadjustedStaticEval = Eval::evaluate(pos);
            ss->staticEval = bestValue = to_corrected_static_eval(unadjustedStaticEval, th, pos);
            if (ttValue != VALUE_NONE
                && (tte->bound() & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
                bestValue = ttValue;
        } else {
            unadjustedStaticEval = (ss - 1)->currentMove != Move::null()
                                 ? Eval::evaluate(pos)
                                 : -(ss - 1)->staticEval;
            ss->staticEval = bestValue = to_corrected_static_eval(unadjustedStaticEval, th, pos);
        }

        if (bestValue >= beta) {
            if (!ttHit)
                tte->save(posKey, value_to_tt(bestValue, ss->ply), false, BOUND_LOWER,
                          DEPTH_NONE, Move::none(), unadjustedStaticEval, TT.generation());
            return bestValue;
        }
        if (bestValue > alpha) alpha = bestValue;
        futilityBase = ss->staticEval + 226;
    }

    const PieceToHistory* contHist[] = {
        (ss - 1)->continuationHistory, (ss - 2)->continuationHistory,
        (ss - 3)->continuationHistory, (ss - 4)->continuationHistory,
        (ss - 5)->continuationHistory, (ss - 6)->continuationHistory
    };

    MovePicker mp(pos, ttMove, depth, &th->mainHistory, &th->captureHistory,
                  contHist, &th->pawnHistory, nullptr);

    Square prevSq = (ss - 1)->currentMove != Move::none() ? (ss - 1)->currentMove.to() : SQ_NONE;

    while ((move = mp.next_move()) != Move::none()) {
        if (!pos.legal(move)) continue;
        ++moveCount;

        bool givesCheck = pos.gives_check(move);
        bool capture = pos.capture(move);

        if (bestValue > VALUE_MATED_IN_MAX_PLY && pos.has_non_pawn_material(pos.side_to_move())) {
            if (!givesCheck && move.to() != prevSq && futilityBase > VALUE_MATED_IN_MAX_PLY
                && move.type() != PROMOTION) {

                if (moveCount > 2) continue;

                Value futilityValue = futilityBase + PieceValue[type_of(pos.piece_on(move.to()))];
                if (futilityValue <= alpha) { bestValue = std::max(bestValue, futilityValue); continue; }

                if (futilityBase <= alpha && !pos.see_ge(move, 1)) {
                    bestValue = std::max(bestValue, futilityBase);
                    continue;
                }
            }

            if (!capture
                && (*contHist[0])[pos.moved_piece(move)][move.to()] < 5389
                && (*contHist[1])[pos.moved_piece(move)][move.to()] < 5389)
                continue;

            if (!pos.see_ge(move, -74)) continue;
        }

        ss->currentMove = move;
        ss->continuationHistory =
            &th->continuationHistory[ss->inCheck][capture][pos.moved_piece(move)][move.to()];

        pos.do_move(move, st, givesCheck);
        th->nodes.fetch_add(1, std::memory_order_relaxed);
        value = -qsearch<nodeType>(pos, ss + 1, -beta, -alpha, depth - 1);
        pos.undo_move(move);

        if (value > bestValue) {
            bestValue = value;
            if (value > alpha) {
                bestMove = move;
                if (PvNode) update_pv(ss->pv, move, (ss + 1)->pv);
                if (value < beta) alpha = value;
                else break;
            }
        }
    }

    if (ss->inCheck && bestValue == -VALUE_INFINITE) return mated_in(ss->ply);

    tte->save(posKey, value_to_tt(bestValue, ss->ply), pvHit,
              bestValue >= beta ? BOUND_LOWER
              : (PvNode && bestValue > oldAlpha) ? BOUND_EXACT : BOUND_UPPER,
              ttDepth, bestMove, unadjustedStaticEval, TT.generation());
    return bestValue;
}

}

void MainThread::check_time() {
    if (--callsCnt > 0) return;
    callsCnt = Limits.nodes ? std::min(512, int(Limits.nodes / 1024)) : 512;

    TimePoint elapsed = Time.elapsed();

    if (ponder) return;

    if ((Limits.use_time_management() && (elapsed > Time.maximum() || stopOnPonderhit))
        || (Limits.movetime && elapsed >= Limits.movetime)
        || (Limits.nodes && Threads.nodes_searched() >= Limits.nodes))
        Threads.stop = true;
}

}
