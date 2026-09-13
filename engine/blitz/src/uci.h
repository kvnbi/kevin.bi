#pragma once
#include "types.h"
#include <iostream>
#include <mutex>
#include <string>

namespace blitz {

class Position;
class Thread;

enum SyncCout { IO_LOCK, IO_UNLOCK };
std::ostream& operator<<(std::ostream& os, SyncCout sc);

#define sync_cout std::cout << blitz::IO_LOCK
#define sync_endl std::endl << blitz::IO_UNLOCK

namespace UCI {

void loop(int argc, char** argv);

std::string value(Value v);
std::string move(Move m, bool chess960);
std::string pv(const Thread& th, int depth);
std::string wdl(Value v, const Position& pos);
Move        to_move(const Position& pos, std::string str);

}

namespace Options {

int  hash_mb();
int  threads();
int  multi_pv();
int  move_overhead();
bool chess960();
bool ponder_enabled();
bool show_wdl();

void set(const std::string& name, const std::string& value);
void print_all();
void init();

}
}
