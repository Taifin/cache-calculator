#include <iostream>
#include <random>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <pthread.h>

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

uintptr_t *mem = static_cast<uintptr_t *>(mmap(nullptr, MAX_M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));

void read(uintptr_t *p, long spots) {
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
    return ct != -1 && nt > mod * ct;
}


bool isMovement(std::map<long long, std::vector<int> > &strideToJumps, long long newStride, long long oldStride) {
    if (!strideToJumps.contains(oldStride) || strideToJumps[oldStride].empty() || strideToJumps[newStride].empty())
        return true;
    return strideToJumps[newStride].front() != strideToJumps[oldStride].front();
}

void printJumps(std::map<long long, std::vector<int> > &strideToJumps) {
    for (const auto &[fst, snd]: strideToJumps) {
        std::cout << "Stride " << fst << ": [";
        for (const auto &e: snd) {
            std::cout << e << " ";
        }
        std::cout << "]\n";
    }
}

template<typename T>
void printVec(std::vector<T> &vec) {
    std::cout << "[";
    for (auto &e: vec) {
        std::cout << e << " ";
    }
    std::cout << "]\n";
}

std::vector<int> measure(long long stride, int maxSpots, double mod = 1.5) {
    std::vector<int> jumps;
    int spots = 1;
    long long oldTime = -1;
    while (spots < maxSpots) {
        long long newTime = time(spots, stride);
        std::cout << "spots " << spots << " stride " << stride << " time: " << newTime << "\n";
        if (jump(oldTime, newTime, mod)) jumps.push_back(spots - 1);
        spots++;
        oldTime = newTime;
    }
    return jumps;
}

long long measure(long long hiStride, long long loStride, int maxSpots, double mod = 1.5) {
    long long combinedStride = hiStride + loStride;
    int spots = 1;
    long long avgTime = 0;
    while (spots < maxSpots) {
        long long newTime = time(spots, combinedStride);
        avgTime += newTime;
        // std::cout << "spots " << spots << " stride " << combinedStride << " time: " << newTime << "\n";
        spots++;
    }
    return avgTime / maxSpots;
}


int main() {
#ifdef TARGET_OS_MAC
    pin_thread_to_core(9);
#endif
    std::map<long long, std::vector<int> > strideToJumps;
    int n = 1 << 6;
    int maxSpots = MAX_ASSOCIATIVITY >> 2;
    int associativityDecreaseStep = 0;
    while (MAX_ASSOCIATIVITY * n < MAX_M) {
    if (associativityDecreaseStep++ == 3) {
            maxSpots >>= 1;
            associativityDecreaseStep = 0;
        }

        auto jumps = measure(n, maxSpots);
        std::cout << "Jumps with stride " << n << " and max assoc " << maxSpots << ": ";
        printVec(jumps);
        strideToJumps[n] = jumps;
        if (isMovement(strideToJumps, n, n / 2)) {
            n <<= 1;
            continue;
        }
        break;
    }
    printJumps(strideToJumps);
    auto max_elem = *std::ranges::max_element(strideToJumps);
    std::pair entities = {max_elem.first / 2, max_elem.second};
    std::cout << "Detected " << max_elem.second.size() << " entities.\n";

    strideToJumps.clear();
    maxSpots = MAX_ASSOCIATIVITY * 2;
    associativityDecreaseStep = 0;
    std::map<long long, char> trend;
    for (long long n = 1 << 5; MAX_ASSOCIATIVITY * n < MAX_M; n <<= 1) {
        if (associativityDecreaseStep++ == 4) {
            maxSpots >>= 1;
            associativityDecreaseStep = 0;
        }

        std::cout << "Max spots: " << maxSpots << "\n";

        auto avgTimeHiStride = measure(n, 0, maxSpots, 1.1);
        std::cout << "Jumps without lo stride and with hi stride " << n << ": " << avgTimeHiStride << '\n';

        int patterns[3] = {}; // d, f, i
        for (long long lo = std::max(1LL, n >> 4); lo < n; lo <<= 1) {
            auto avgTimeHiLoStride = measure(n, lo, maxSpots, 1.1);
            std::cout << "Jumps with lo stride " << lo << ": " << avgTimeHiLoStride << '\n';

            double ratio = (double)avgTimeHiStride / (double)avgTimeHiLoStride;
            std::cout << "Ratio: " << ratio << '\n';
            if (0.95 < ratio && ratio < 1.05) {
                patterns[1]++;
            } else if (avgTimeHiStride > avgTimeHiLoStride) {
                patterns[2]++;
            } else {
                patterns[0]++;
            }
            std::cout << "Patterns: " << patterns[0] << " " << patterns[1] << " " << patterns[2] << '\n';
        }

        if (patterns[0] == 0 && patterns[1] == 0 && patterns[2] == 0) {
            continue;
        }
        std::cout << "Patterns: " << patterns[0] << " " << patterns[1] << " " << patterns[2] << '\n';

        char pattern = 'U';
        if (patterns[2] > patterns[1] && patterns[2] > patterns[0]) {
            pattern = 'I';
        }
        if (patterns[1] > patterns[0] && patterns[1] >= patterns[2]) {
            pattern = 'F';
        }
        if (patterns[0] > patterns[1] && patterns[0] >= patterns[2]) {
            pattern = 'D';
        }
        std::cout << "Pattern: " << pattern << "\n";
        trend[n] = pattern;

        if (trend.size() > 4) {
            bool changed = false;
            char prev = trend[n >> 4];
            for (int j = 1; j < 4; j++) {
                if (trend[n >> j] != prev) {
                    changed = true;
                }
            }
            if (!changed) break;
        }
    }
    std::cout << "Trend: ";
    for (const auto &e: trend) {
        std::cout << "Stride " << e.first << " pattern " << e.second << "\n";
    }

    long long dIndex = -1;
    bool iFound = false;
    int ind = 0;
    for (auto &e: trend) {
        if (e.second == 'D' && !iFound) {
            dIndex = e.first;
        } else {
            dIndex = -1;
            break;
        }

        if (e.second == 'I') {
            iFound = true;

            if (dIndex == -1) {
                break;
            }
        }

        ind++;
    }

    if (dIndex == -1) {
        std::cout << "Unable to determine cache line size\n";
    } else {
        std::cout << "Supposed cache line size: " << dIndex << "\n";
    }
}
