#pragma once
#include "types.h"
#include <string>

namespace blitz {

class Position;

namespace datagen {

struct alignas(8) Sample {
    u64 occ;
    u8  pieces[16];
    i16 score;
    u8  result;
    u8  stm;
    u8  pad[4];
};
static_assert(sizeof(Sample) == 32, "sample must stay 32 bytes");

Sample encode(const Position& pos, Value score);

void run(int argc, char** argv);

}
}
