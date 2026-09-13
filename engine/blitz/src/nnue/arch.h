#pragma once
#include "../types.h"

namespace blitz::nnue {

#ifndef BLITZ_HL
    #define BLITZ_HL 128
#endif

constexpr int FT_IN          = 768;
constexpr int KING_BUCKETS   = 8;
constexpr int HL             = BLITZ_HL;
constexpr int OUTPUT_BUCKETS = 8;

static_assert(HL % 64 == 0, "HL must be a multiple of 64 (see screlu_dot blocking)");

constexpr int QA = 255;
constexpr int QB = 64;
constexpr int NET_SCALE = 400 * 2;

constexpr int KingBucket[64] = {
    0, 0, 1, 1, 1, 1, 0, 0,
    2, 2, 3, 3, 3, 3, 2, 2,
    4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7,
};

struct alignas(64) Accumulator {
    i16  values[COLOR_NB][HL];
    bool computed[COLOR_NB];
};

struct DirtyPiece {
    int    dirty_num;
    Piece  piece[3];
    Square from[3];
    Square to[3];
};

}
