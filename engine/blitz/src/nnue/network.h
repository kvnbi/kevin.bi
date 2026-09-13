#pragma once
#include "arch.h"
#include <string>

namespace blitz {
class Position;
struct StateInfo;
}

namespace blitz::nnue {

int feature_index(Color persp, Square ksq, Piece pc, Square sq);

bool needs_refresh(Color persp, Square from, Square to);

constexpr const char* EmbeddedName = "<embedded>";

bool  available();
bool  load(const std::string& path);
bool  load_embedded();
void  unload();
std::string net_name();

void refresh_accumulator(const Position& pos, StateInfo* st);

Value evaluate(const Position& pos);

Value evaluate_from_scratch(const Position& pos);

}
