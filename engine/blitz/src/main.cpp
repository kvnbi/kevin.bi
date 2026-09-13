#include "bitboard.h"
#include "datagen.h"
#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "uci.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace blitz;

    set_binary_directory(argv[0]);
    std::cout << engine_info() << std::endl;

    bitboards_init();
    Position::init();
    Search::init();
    Options::init();

    if (argc > 1 && std::string(argv[1]) == "datagen") {
        datagen::run(argc, argv);
        Threads.set(0);
        return 0;
    }

    UCI::loop(argc, argv);

    Threads.set(0);
    return 0;
}
