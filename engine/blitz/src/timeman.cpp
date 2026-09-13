#include "timeman.h"
#include "uci.h"
#include <algorithm>
#include <cmath>

namespace blitz {

TimeManager Time;

void TimeManager::init(Search::LimitsType& limits, Color us, int ply) {
    startTime_ = limits.startTime;

    if (limits.movetime) {
        optimumTime_ = maximumTime_ = limits.movetime - Options::move_overhead();
        optimumTime_ = maximumTime_ = std::max<TimePoint>(optimumTime_, 1);
        return;
    }

    if (!limits.use_time_management()) {
        optimumTime_ = maximumTime_ = 0;
        return;
    }

    const TimePoint moveOverhead = Options::move_overhead();

    const int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : 50;

    TimePoint timeLeft = std::max<TimePoint>(1,
        limits.time[us] + limits.inc[us] * (mtg - 1) - moveOverhead * (2 + mtg));

    double optScale, maxScale;

    if (limits.movestogo == 0) {

        double ratio = double(limits.time[us]) / std::max<TimePoint>(1, timeLeft);
        optScale = std::min(0.0120 + std::pow(ply + 3.0, 0.44) * 0.0039,
                            0.20 * ratio);
        maxScale = std::min(6.5, 3.0 + ply / 12.0);
    } else {
        double ratio = double(limits.time[us]) / std::max<TimePoint>(1, timeLeft);
        optScale = std::min((0.88 + ply / 116.4) / mtg, 0.88 * ratio);
        maxScale = std::min(6.3, 1.5 + 0.11 * mtg);
    }

    optimumTime_ = TimePoint(optScale * timeLeft);
    maximumTime_ = TimePoint(std::min(0.84 * limits.time[us] - moveOverhead,
                                      maxScale * optimumTime_)) - 10;

    optimumTime_ = std::max<TimePoint>(optimumTime_, 1);
    maximumTime_ = std::max<TimePoint>(maximumTime_, 1);
}

}
