#pragma once
#include "misc.h"
#include "search.h"

namespace blitz {

class TimeManager {
public:
    void init(Search::LimitsType& limits, Color us, int ply);

    TimePoint optimum() const { return optimumTime_; }
    TimePoint maximum() const { return maximumTime_; }
    TimePoint elapsed() const { return now() - startTime_; }

private:
    TimePoint startTime_ = 0;
    TimePoint optimumTime_ = 0;
    TimePoint maximumTime_ = 0;
};

extern TimeManager Time;

}
