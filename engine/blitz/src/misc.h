#pragma once
#include "types.h"
#include <chrono>
#include <string>
#include <vector>

namespace blitz {

using TimePoint = std::chrono::milliseconds::rep;

inline TimePoint now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void prefetch(const void* addr) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr);
#else
    (void)addr;
#endif
}

void* aligned_large_pages_alloc(size_t size);
void  aligned_large_pages_free(void* ptr);

std::vector<std::string> split(const std::string& s, char delim);
void set_binary_directory(const char* argv0);
std::string binary_directory();
std::string engine_info();

}
