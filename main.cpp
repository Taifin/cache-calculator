#include <iostream>
#include <random>
#include <sys/mman.h>
#include <map>
#include <algorithm>
#include <chrono>
#include <ranges>

#ifdef TARGET_OS_MAC
#include "mach//mach.h"

void pin_thread_to_core(int core_id) {
    thread_affinity_policy_data_t policy = {core_id};
    thread_policy_set(mach_thread_self(),
                      THREAD_AFFINITY_POLICY,
                      (thread_policy_t) &policy,
                      THREAD_AFFINITY_POLICY_COUNT);
}
#endif

constexpr int MAX_ASSOCIATIVITY = 4096;
constexpr long long MAX_STRIDE = 1 << 21;
constexpr long long MAX_M = 1 << 30;
constexpr int N_READS = 1'000'000;

#ifdef DEBUG
#define DEBUG_CERR(msg) \
    std::cerr << msg;
#else
#define DEBUG_CERR(msg) \
    ;
#endif

uintptr_t *mem = static_cast<uintptr_t *>(mmap(nullptr, MAX_M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));

void read(const uintptr_t *p, long spots) {
    for (int i = 0; i < N_READS; i++) {
        p = (uintptr_t *) *p;
        asm volatile("" : "+r"(p));
    }
}

long long time(long spots, long long stride) {
    if (mem == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    std::random_device rd;
    std::mt19937 rng(rd());
    std::vector<int> ind(spots);
    std::iota(ind.begin(), ind.end(), 0);
    std::ranges::shuffle(ind, rng);

    uintptr_t *head = mem + ind[0] * (stride / sizeof(uintptr_t));
    uintptr_t *ptr = head;
    for (int k = 1; k < spots; k++) {
        uintptr_t *next = mem + ind[k] * (stride / sizeof(uintptr_t));
        *ptr = (uintptr_t) next;
        ptr = next;
    }
    *ptr = (uintptr_t) head; // close the ring

    read(mem, spots);

    std::chrono::time_point before = std::chrono::steady_clock::now();
    read(mem, spots);
    std::chrono::time_point after = std::chrono::steady_clock::now();

    return std::chrono::duration_cast<std::chrono::microseconds>(after - before).count();
}

bool jump(long long ct, long long nt, double mod = 1.5) {
    return ct != -1 && (double) nt > mod * (double) ct;
}


bool isMovement(std::map<int, std::vector<int> > &strideToJumps, int newStride, int oldStride) {
    if (!strideToJumps.contains(oldStride) || strideToJumps[oldStride].empty() || strideToJumps[newStride].empty())
        return true;
    return strideToJumps[newStride].front() != strideToJumps[oldStride].front();
}

void printJumps(std::vector<int> &jumps, int stride, int spots) {
    DEBUG_CERR("Jumps with stride " << stride << " and max assoc " << spots << ": [")
    for (const auto &e: jumps) {
        DEBUG_CERR(e << ", ")
    }
    DEBUG_CERR("]\n");
}

std::vector<int> measure(int stride, int maxSpots, double mod = 3) {
    std::vector<int> jumps;
    int spots = 1;
    long long oldTime = -1;
    while (spots++ < maxSpots) {
        long long newTime = time(spots, stride);
        if (jump(oldTime, newTime, mod)) jumps.push_back(spots - 1);
        oldTime = newTime;
    }
    return jumps;
}

long long measure(int hiStride, int loStride, int maxSpots) {
    long long combinedStride = hiStride + loStride;
    int spots = 1;
    long long avgTime = 0;
    while (spots++ < maxSpots) {
        long long newTime = time(spots, combinedStride);
        avgTime += newTime;
    }
    return avgTime / maxSpots;
}

std::map<int, long long> findFirstOccurrences(const std::map<int, std::vector<int>>& powerMap) {
    if (powerMap.empty()) {
        return {};
    }

    auto lastEntry = powerMap.rbegin();
    const std::vector<int>& targetIndices = lastEntry->second;

    std::map<int, long long> result;
    for (int targetIndex : targetIndices) {
        long long firstOccurrenceFromEnd = -1;

        for (const auto &[fst, snd] : std::ranges::reverse_view(powerMap)) {
            long long currentKey = fst;
            const std::vector<int>& currentIndices = snd;

            bool found = false;
            for (int idx : currentIndices) {
                if (idx == targetIndex) {
                    found = true;
                    break;
                }
            }

            if (found) {
                firstOccurrenceFromEnd = currentKey;
            } else {
                break;
            }
        }

        if (firstOccurrenceFromEnd != -1) {
            result[targetIndex] = firstOccurrenceFromEnd;
        }

    }

    return result;
}


int main(int argc, char* argv[]) {
#ifdef TARGET_OS_MAC
    pin_thread_to_core(9);
#endif

    std::map<int, std::vector<int> > strideToJumps;
    int stride = 1 << 8;
    int maxSpots = MAX_ASSOCIATIVITY >> 2;
    int associativityDecreaseStep = 0;
    while (MAX_ASSOCIATIVITY * stride < MAX_M) {
        if (associativityDecreaseStep++ == 3) {
            maxSpots >>= 1;
            associativityDecreaseStep = 0;
        }

        auto jumps = measure(stride, maxSpots);
        printJumps(jumps, stride, maxSpots);

        strideToJumps[stride] = jumps;
        if (isMovement(strideToJumps, stride, stride / 2)) {
            stride <<= 1;
            continue;
        }
        break;
    }
    auto entities = findFirstOccurrences(strideToJumps);
    if (entities.size() == 1) {
        std::cout << "Cache size is " << entities.begin()->first * entities.begin()->second << " bytes" << std::endl;
        std::cout << "Cache associativity is " << entities.begin()->second << std::endl;
    } else {
        std::cout << "Detected " << entities.size() << " entities, one of which is the cache." << std::endl;
        for (auto& [stride, assoc] : entities) {
            std::cout << "\tEntity with size " << assoc * stride << " and associativity " << assoc << std::endl;
        }
    }

    maxSpots = MAX_ASSOCIATIVITY * 2;
    associativityDecreaseStep = 0;
    std::map<int, int> trend;
    for (stride = 1 << 4; stride <= 256; stride <<= 1) {
        if (associativityDecreaseStep++ == 4) {
            maxSpots >>= 1;
            associativityDecreaseStep = 0;
        }

        DEBUG_CERR("Max spots: " << maxSpots << "\n")

        auto avgTimeHiStride = measure(stride, 0LL, maxSpots);
        DEBUG_CERR("Jumps without lo stride and with hi stride " << stride << ": " << avgTimeHiStride << '\n')

        int patterns[2] = {}; // d, i
        for (int lo = std::max(1, stride >> 4); lo < stride; lo <<= 1) {
            auto avgTimeHiLoStride = measure(stride, lo, maxSpots);
            DEBUG_CERR("Jumps with lo stride " << lo << ": " << avgTimeHiLoStride << '\n')

            double ratio = (double) avgTimeHiStride / (double) avgTimeHiLoStride;
            DEBUG_CERR("Ratio: " << ratio << '\n')
            if (0.98 < ratio && ratio < 1.02) {
                continue;
            }

            if (avgTimeHiStride > avgTimeHiLoStride) {
                patterns[1]++;
            } else {
                patterns[0]++;
            }
            DEBUG_CERR("Patterns: " << patterns[0] << " " << patterns[1] << '\n')
        }

        if (patterns[0] == 0 && patterns[1] == 0) {
            continue;
        }

        int pattern = 0;
        if (patterns[1] > patterns[0]) {
            pattern = 1;
        }
        if (patterns[0] > patterns[1]) {
            pattern = -1;
        }
        DEBUG_CERR("Pattern: " << pattern << "\n")
        trend[stride] = pattern;
    }
    DEBUG_CERR("Trend: \n")
    for (const auto &e: trend) {
        DEBUG_CERR("\tStride " << e.first << " pattern " << e.second << "\n")
    }

    int prev = -1;
    int lineSize = -1;
    for (auto &e: trend) {
        if (lineSize == -1 && e.second == 1) {
            lineSize = e.first;
        }

        if (e.second >= prev) {
            prev = e.second;
            continue;
        }

        lineSize = -1;
        break;
    }

    if (lineSize == -1) {
        std::cout << "Unable to determine cache line size\n";
    } else {
        std::cout << "Cache line size: " << lineSize << "bytes.\n";
    }
}
