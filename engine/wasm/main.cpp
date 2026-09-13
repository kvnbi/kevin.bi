#include "bitboard.h"
#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "uci.h"
#include <emscripten.h>
#include <string>

namespace {

blitz::Position g_pos;

}

extern "C" {

EMSCRIPTEN_KEEPALIVE void blitz_init() {
    using namespace blitz;
    set_binary_directory("");
    bitboards_init();
    Position::init();
    Search::init();
    Options::init();
    UCI::init_position(g_pos);
}

EMSCRIPTEN_KEEPALIVE void blitz_command(const char* cmd) {
    blitz::UCI::execute(g_pos, std::string(cmd));
}

}
