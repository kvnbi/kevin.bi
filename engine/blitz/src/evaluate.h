#pragma once
#include "types.h"
#include <string>

namespace blitz {

class Position;

namespace Eval {

Value evaluate(const Position& pos);

std::string trace(const Position& pos);

}
}
