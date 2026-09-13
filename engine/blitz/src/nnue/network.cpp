#include "network.h"
#include "../position.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#if defined(__ARM_NEON)
    #include <arm_neon.h>
    #define BLITZ_NEON 1
#elif defined(__AVX2__)
    #include <immintrin.h>
    #define BLITZ_AVX2 1
#endif

#if defined(__APPLE__)
    #define BLITZ_NET_SECTION ".const_data\n"
    #define BLITZ_NET_SYMBOL(name) "_" name
#elif defined(_WIN32)
    #define BLITZ_NET_SECTION ".section .rdata,\"dr\"\n"
    #define BLITZ_NET_SYMBOL(name) name
#else
    #define BLITZ_NET_SECTION ".section .rodata\n"
    #define BLITZ_NET_SYMBOL(name) name
#endif

__asm__(BLITZ_NET_SECTION
        ".global " BLITZ_NET_SYMBOL("blitz_embedded_net") "\n"
        ".balign 64\n"
        BLITZ_NET_SYMBOL("blitz_embedded_net") ":\n"
        ".incbin \"blitz.nnue\"\n"
        "Lblitz_embedded_net_end:\n"
        ".global " BLITZ_NET_SYMBOL("blitz_embedded_net_size") "\n"
        ".balign 8\n"
        BLITZ_NET_SYMBOL("blitz_embedded_net_size") ":\n"
        ".quad Lblitz_embedded_net_end - " BLITZ_NET_SYMBOL("blitz_embedded_net") "\n"
        ".text\n");

extern "C" const unsigned char blitz_embedded_net[];
extern "C" const unsigned long long blitz_embedded_net_size;

namespace blitz::nnue {

namespace {

constexpr u32 NET_MAGIC   = 0x564E4554;
constexpr u32 NET_VERSION = 1;

struct Network {
    alignas(64) i16 ftWeights[KING_BUCKETS * FT_IN][HL];
    alignas(64) i16 ftBias[HL];
    alignas(64) i16 outWeights[OUTPUT_BUCKETS][2 * HL];
    alignas(64) i16 outBias[OUTPUT_BUCKETS];
};

Network*    g_net = nullptr;
std::string g_name;

inline Square orient(Color persp, Square s, Square ksq) {

    int x = int(s) ^ (int(persp) * 56);
    if (file_of(ksq) >= FILE_E) x ^= 7;
    return Square(x);
}

inline void acc_add(i16* acc, const i16* w) {
#if defined(BLITZ_NEON)
    for (int i = 0; i < HL; i += 8)
        vst1q_s16(acc + i, vaddq_s16(vld1q_s16(acc + i), vld1q_s16(w + i)));
#elif defined(BLITZ_AVX2)
    for (int i = 0; i < HL; i += 16)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(acc + i),
                            _mm256_add_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(acc + i)),
                                             _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i))));
#else
    for (int i = 0; i < HL; ++i) acc[i] = i16(acc[i] + w[i]);
#endif
}

inline void acc_sub(i16* acc, const i16* w) {
#if defined(BLITZ_NEON)
    for (int i = 0; i < HL; i += 8)
        vst1q_s16(acc + i, vsubq_s16(vld1q_s16(acc + i), vld1q_s16(w + i)));
#elif defined(BLITZ_AVX2)
    for (int i = 0; i < HL; i += 16)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(acc + i),
                            _mm256_sub_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(acc + i)),
                                             _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i))));
#else
    for (int i = 0; i < HL; ++i) acc[i] = i16(acc[i] - w[i]);
#endif
}

template <int NAdd, int NSub>
inline void acc_apply(i16* dst, const i16* src,
                      const i16* const* add, const i16* const* sub) {
#if defined(BLITZ_NEON)
    for (int i = 0; i < HL; i += 8) {
        int16x8_t v = vld1q_s16(src + i);
        for (int a = 0; a < NAdd; ++a) v = vaddq_s16(v, vld1q_s16(add[a] + i));
        for (int b = 0; b < NSub; ++b) v = vsubq_s16(v, vld1q_s16(sub[b] + i));
        vst1q_s16(dst + i, v);
    }
#elif defined(BLITZ_AVX2)
    for (int i = 0; i < HL; i += 16) {
        __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        for (int a = 0; a < NAdd; ++a)
            v = _mm256_add_epi16(v, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(add[a] + i)));
        for (int b = 0; b < NSub; ++b)
            v = _mm256_sub_epi16(v, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sub[b] + i)));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), v);
    }
#else
    for (int i = 0; i < HL; ++i) {
        int v = src[i];
        for (int a = 0; a < NAdd; ++a) v += add[a][i];
        for (int b = 0; b < NSub; ++b) v -= sub[b][i];
        dst[i] = i16(v);
    }
#endif
}

i64 screlu_dot(const i16* acc, const i16* w, int n) {
#if defined(BLITZ_NEON)
    constexpr int FlushEvery = 64;
    const int16x8_t lo = vdupq_n_s16(0), hi = vdupq_n_s16(QA);
    int64x2_t total = vdupq_n_s64(0);

    for (int base = 0; base < n; base += FlushEvery) {
        int32x4_t s0 = vdupq_n_s32(0), s1 = vdupq_n_s32(0);
        for (int i = base; i < base + FlushEvery; i += 8) {
            int16x8_t v  = vminq_s16(vmaxq_s16(vld1q_s16(acc + i), lo), hi);
            int16x8_t wv = vld1q_s16(w + i);

            int32x4_t p0 = vmull_s16(vget_low_s16(v), vget_low_s16(wv));
            int32x4_t p1 = vmull_high_s16(v, wv);
            s0 = vmlaq_s32(s0, p0, vmovl_s16(vget_low_s16(v)));
            s1 = vmlaq_s32(s1, p1, vmovl_high_s16(v));
        }
        total = vpadalq_s32(total, s0);
        total = vpadalq_s32(total, s1);
    }
    return vgetq_lane_s64(total, 0) + vgetq_lane_s64(total, 1);
#elif defined(BLITZ_AVX2)
    constexpr int FlushEvery = 64;
    const __m256i zero = _mm256_setzero_si256(), hi = _mm256_set1_epi16(QA);
    __m256i total = _mm256_setzero_si256();

    for (int base = 0; base < n; base += FlushEvery) {
        __m256i s = _mm256_setzero_si256();
        for (int i = base; i < base + FlushEvery; i += 16) {
            __m256i v  = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(acc + i));
            __m256i wv = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
            v = _mm256_min_epi16(_mm256_max_epi16(v, zero), hi);

            __m256i v0 = _mm256_unpacklo_epi16(v, zero);
            __m256i v1 = _mm256_unpackhi_epi16(v, zero);
            __m256i w0 = _mm256_srai_epi32(_mm256_unpacklo_epi16(zero, wv), 16);
            __m256i w1 = _mm256_srai_epi32(_mm256_unpackhi_epi16(zero, wv), 16);

            s = _mm256_add_epi32(s, _mm256_mullo_epi32(_mm256_mullo_epi32(v0, w0), v0));
            s = _mm256_add_epi32(s, _mm256_mullo_epi32(_mm256_mullo_epi32(v1, w1), v1));
        }
        total = _mm256_add_epi64(total, _mm256_cvtepi32_epi64(_mm256_castsi256_si128(s)));
        total = _mm256_add_epi64(total, _mm256_cvtepi32_epi64(_mm256_extracti128_si256(s, 1)));
    }

    alignas(32) i64 lanes[4];
    _mm256_store_si256(reinterpret_cast<__m256i*>(lanes), total);
    return lanes[0] + lanes[1] + lanes[2] + lanes[3];
#else
    i64 sum = 0;
    for (int i = 0; i < n; ++i) {
        i32 v = std::clamp(i32(acc[i]), 0, i32(QA));
        sum += i64(v) * v * w[i];
    }
    return sum;
#endif
}

}

int feature_index(Color persp, Square ksq, Piece pc, Square sq) {
    int bucket   = KingBucket[orient(persp, ksq, ksq)];
    int relColor = (color_of(pc) == persp) ? 0 : 1;
    int pieceIdx = relColor * 6 + (int(type_of(pc)) - 1);
    return bucket * FT_IN + pieceIdx * 64 + int(orient(persp, sq, ksq));
}

bool needs_refresh(Color persp, Square from, Square to) {

    if ((file_of(from) >= FILE_E) != (file_of(to) >= FILE_E)) return true;
    return KingBucket[orient(persp, from, from)] != KingBucket[orient(persp, to, to)];
}

bool available() { return g_net != nullptr; }
std::string net_name() { return g_name; }

void unload() { delete g_net; g_net = nullptr; g_name.clear(); }

namespace {

bool load_from(const unsigned char* data, size_t size, const std::string& name) {
    constexpr size_t HeaderSize = 5 * sizeof(u32);
    if (size != HeaderSize + sizeof(Network::ftWeights) + sizeof(Network::ftBias)
                             + sizeof(Network::outWeights) + sizeof(Network::outBias))
        return false;

    u32 header[5];
    std::memcpy(header, data, HeaderSize);
    if (header[0] != NET_MAGIC || header[1] != NET_VERSION || header[2] != u32(HL)
        || header[3] != u32(KING_BUCKETS) || header[4] != u32(OUTPUT_BUCKETS))
        return false;

    Network* net = new (std::align_val_t(64)) Network;
    const unsigned char* p = data + HeaderSize;
    std::memcpy(net->ftWeights, p, sizeof(net->ftWeights));   p += sizeof(net->ftWeights);
    std::memcpy(net->ftBias, p, sizeof(net->ftBias));         p += sizeof(net->ftBias);
    std::memcpy(net->outWeights, p, sizeof(net->outWeights)); p += sizeof(net->outWeights);
    std::memcpy(net->outBias, p, sizeof(net->outBias));

    unload();
    g_net = net;
    g_name = name;
    return true;
}

}

bool load_embedded() {
    return load_from(blitz_embedded_net, size_t(blitz_embedded_net_size), EmbeddedName);
}

bool load(const std::string& path) {
    if (path == EmbeddedName) return load_embedded();

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return load_from(bytes.data(), bytes.size(), path);
}

void refresh_accumulator(const Position& pos, StateInfo* st) {
    for (Color persp : { WHITE, BLACK }) {
        i16* acc = st->accumulator.values[persp];
        std::memcpy(acc, g_net->ftBias, sizeof(g_net->ftBias));

        Square ksq = pos.king_square(persp);
        for (Bitboard b = pos.pieces(); b;) {
            Square s = pop_lsb(b);
            acc_add(acc, g_net->ftWeights[feature_index(persp, ksq, pos.piece_on(s), s)]);
        }
        st->accumulator.computed[persp] = true;
    }
}

namespace {

void update_accumulator(const Position& pos, StateInfo* st, Color persp) {
    if (st->accumulator.computed[persp]) return;

    Square ksq = pos.king_square(persp);

    constexpr int MaxReplay = 32;
    StateInfo* chain[MaxReplay];
    int chainLen = 0;
    StateInfo* base = st;
    bool reusable = false;

    while (true) {
        if (base->accumulator.computed[persp]) { reusable = true; break; }
        if (!base->previous || chainLen >= MaxReplay) break;

        const auto& dp = base->dirtyPiece;
        if (dp.dirty_num && type_of(dp.piece[0]) == KING && color_of(dp.piece[0]) == persp
            && needs_refresh(persp, dp.from[0], dp.to[0]))
            break;

        chain[chainLen++] = base;
        base = base->previous;
    }

    if (!reusable) {
        i16* acc = st->accumulator.values[persp];
        std::memcpy(acc, g_net->ftBias, sizeof(g_net->ftBias));
        for (Bitboard b = pos.pieces(); b;) {
            Square s = pop_lsb(b);
            acc_add(acc, g_net->ftWeights[feature_index(persp, ksq, pos.piece_on(s), s)]);
        }
        st->accumulator.computed[persp] = true;
        return;
    }

    for (int c = chainLen - 1; c >= 0; --c) {
        StateInfo* cur = chain[c];
        i16* dst = cur->accumulator.values[persp];
        const i16* src = cur->previous->accumulator.values[persp];

        const i16* add[2];
        const i16* sub[2];
        int nAdd = 0, nSub = 0;

        const auto& dp = cur->dirtyPiece;
        for (int i = 0; i < dp.dirty_num; ++i) {
            if (dp.from[i] != SQ_NONE && nSub < 2)
                sub[nSub++] = g_net->ftWeights[feature_index(persp, ksq, dp.piece[i], dp.from[i])];
            if (dp.to[i] != SQ_NONE && nAdd < 2)
                add[nAdd++] = g_net->ftWeights[feature_index(persp, ksq, dp.piece[i], dp.to[i])];
        }

        if (nAdd == 1 && nSub == 1)      acc_apply<1, 1>(dst, src, add, sub);
        else if (nAdd == 1 && nSub == 2) acc_apply<1, 2>(dst, src, add, sub);
        else if (nAdd == 2 && nSub == 2) acc_apply<2, 2>(dst, src, add, sub);
        else if (nAdd == 0 && nSub == 0) std::memcpy(dst, src, sizeof(i16) * HL);
        else {

            std::memcpy(dst, src, sizeof(i16) * HL);
            for (int a = 0; a < nAdd; ++a) acc_add(dst, add[a]);
            for (int b = 0; b < nSub; ++b) acc_sub(dst, sub[b]);
        }
        cur->accumulator.computed[persp] = true;
    }
}

}

namespace {

Value output(const Position& pos, const i16* ours, const i16* theirs) {

    int bucket = (pos.count_all() - 2) / ((32 - 2 + OUTPUT_BUCKETS - 1) / OUTPUT_BUCKETS);
    bucket = std::min(bucket, OUTPUT_BUCKETS - 1);

    i64 sum = screlu_dot(ours, g_net->outWeights[bucket], HL)
            + screlu_dot(theirs, g_net->outWeights[bucket] + HL, HL);

    i64 v = (sum / QA + g_net->outBias[bucket]) * NET_SCALE / (QA * QB);
    return Value(std::clamp<i64>(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

}

Value evaluate_from_scratch(const Position& pos) {
    alignas(64) i16 acc[COLOR_NB][HL];
    for (Color persp : { WHITE, BLACK }) {
        std::memcpy(acc[persp], g_net->ftBias, sizeof(g_net->ftBias));
        Square ksq = pos.king_square(persp);
        for (Bitboard b = pos.pieces(); b;) {
            Square s = pop_lsb(b);
            acc_add(acc[persp], g_net->ftWeights[feature_index(persp, ksq, pos.piece_on(s), s)]);
        }
    }
    Color us = pos.side_to_move();
    return output(pos, acc[us], acc[~us]);
}

Value evaluate(const Position& pos) {
    StateInfo* st = pos.state();
    update_accumulator(pos, st, WHITE);
    update_accumulator(pos, st, BLACK);

    Color us = pos.side_to_move();
    const i16* ours   = st->accumulator.values[us];
    const i16* theirs = st->accumulator.values[~us];

    return output(pos, ours, theirs);
}

}
