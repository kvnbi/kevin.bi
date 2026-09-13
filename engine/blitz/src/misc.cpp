#include "misc.h"
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(_WIN32)
    #include <malloc.h>
#else
    #include <sys/mman.h>
#endif

namespace blitz {

void* aligned_large_pages_alloc(size_t size) {
    constexpr size_t Alignment = 4096;
    size = (size + Alignment - 1) / Alignment * Alignment;

#if defined(_WIN32)
    return _aligned_malloc(size, Alignment);
#else
    void* mem = nullptr;
    if (posix_memalign(&mem, Alignment, size) != 0) return nullptr;
    #if defined(__linux__)
    madvise(mem, size, MADV_HUGEPAGE);
    #endif
    return mem;
#endif
}

void aligned_large_pages_free(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim))
        if (!item.empty()) out.push_back(item);
    return out;
}

std::string g_binaryDir;

void set_binary_directory(const char* argv0) {
    std::string s(argv0 ? argv0 : "");
    size_t pos = s.find_last_of("/\\");
    g_binaryDir = pos == std::string::npos ? "" : s.substr(0, pos + 1);
}

std::string binary_directory() { return g_binaryDir; }

std::string engine_info() { return "Blitz 2.2"; }

}
