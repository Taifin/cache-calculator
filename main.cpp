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
    thread_affinity_policy_data_t policy = { core_id };
    thread_policy_set(mach_thread_self(),
                      THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&policy,
                      THREAD_AFFINITY_POLICY_COUNT);
}
#endif


constexpr int MAX_ASSOCIATIVITY = 1000;
constexpr long long MAX_STRIDE = 1 << 21;
constexpr long long MAX_M = 1 << 30;
constexpr int N_READS = 1'000'000;

uint64_t *mem = static_cast<uint64_t *>(mmap(nullptr, MAX_M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));


void read(uint64_t *p, long spots) {
    for (int i = 0; i < N_READS; i++) {
        p = (uint64_t *) *p;
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

    uint64_t *ptr = mem;
    for (int i = 0; i < spots; i++) {
        *ptr = (uint64_t)mem + ind[i] * stride;
        ptr = (uint64_t*) *ptr;
    }
    *ptr = (uint64_t)mem + ind[0] * stride;


    read(mem, spots);

    std::chrono::time_point before = std::chrono::steady_clock::now();
    read(mem, spots);
    std::chrono::time_point after = std::chrono::steady_clock::now();

    return std::chrono::duration_cast<std::chrono::nanoseconds>(after - before).count() / N_READS;
}

bool jump(long long ct, long long nt) {
    return ct != -1 && nt > 1.9 * ct;
}


bool isMovement(std::map<long long, std::vector<long long>>& strideToJumps, long long newStride, long long oldStride) {
    if (!strideToJumps.contains(oldStride)) return true;
    return strideToJumps[newStride] != strideToJumps[oldStride];
}

void printJumps(std::map<long long, std::vector<long long>>& strideToJumps) {
    for (const auto &[fst, snd]: strideToJumps) {
        std::cout << "Stride " << fst << ": [";
        for (const auto &e : snd) {
            std::cout << e << " ";
        }
        std::cout << "]\n";
    }
}

template <typename T>
void printVec(std::vector<T> &vec) {
    std::cout << "[";
    for (auto &e : vec) {
        std::cout << e << " ";
    }
    std::cout << "]\n";
}

std::vector<int> measure(long long hiStride, long long loStride) {
    long long combinedStride = hiStride + loStride;
    std::vector<int> jumps;
    int spots = 1;
    long long oldTime = -1;
    while (spots < MAX_ASSOCIATIVITY) {
        long long newTime = time(spots, combinedStride);
        // std::cout << "spots " << spots << " stride " << combinedStride << " time: " << newTime << "\n";
        if (jump(oldTime, newTime)) jumps.push_back(spots);
        spots++;
        oldTime = newTime;
    }
    return jumps;
}


int main() {
#ifdef TARGET_OS_MAC
    pin_thread_to_core(9);
#endif
    std::map<long long, std::vector<long long>> strideToJumps;
    int n = 1 << 6;
    while (MAX_ASSOCIATIVITY * n < MAX_M) {
        int s = 1;
        long long oldTime = -1;
        while (s < MAX_ASSOCIATIVITY) {
            long long newTime = time(s, n);
            std::cout << "s " << s << " n " << n << " time: " << newTime << "\n";
            if (jump(oldTime, newTime)) {
                if (!strideToJumps.contains(n)) strideToJumps[n] = { s - 1 };
                else strideToJumps[n].push_back(s - 1);
            }
            s++;
            oldTime = newTime;
        }
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
    for (int i = 0; i < entities.second.size(); i++) {
        std::cout << "\t Supposed entity " << i << " size = " << entities.first * entities.second[i] << "\n";
        std::map<long long, char> trend;
        for (long long n = 1 << 11; MAX_ASSOCIATIVITY * n < MAX_M; n <<= 1) {
            auto jumpsWithoutLoStride = measure(n, 0);
            std::cout << "Jumps without lo stride and with hi stride " << n << ": ";
            printVec(jumpsWithoutLoStride);

            int patterns[3] = {}; // d, f, i
            for (long long lo = std::max(1LL, n >> 4); lo < n; lo <<= 1) {
                auto jumpsWithLoStride = measure(n, lo);
                std::cout << "Jumps with lo stride " << lo << ": ";
                printVec(jumpsWithLoStride);

                if (jumpsWithoutLoStride.empty()) break;
                if (jumpsWithLoStride.empty()) continue;

                auto iThJump = jumpsWithoutLoStride[i];
                auto iThJumpLo = jumpsWithLoStride[i];
                if (iThJumpLo > iThJump) {
                    patterns[2]++;
                }
                if (iThJumpLo == iThJump) {
                    patterns[1]++;
                }
                if (iThJumpLo < iThJump) {
                    patterns[0]++;
                }
            }

            if (patterns[0] == 0 && patterns[1] == 0 && patterns[2] == 0) {
                continue;
            }

            char pattern;
            if (patterns[2] > patterns[1] && patterns[2] > patterns[0]) {
                pattern = 'I';
            }
            if (patterns[1] > patterns[0] && patterns[1] > patterns[2]) {
                pattern = 'F';
            }
            if (patterns[0] > patterns[1] && patterns[0] > patterns[2]) {
                pattern = 'D';
            }
            std::cout << "Pattern: " << pattern << "\n";
            trend[n] = pattern;

            if (trend.size() > 6) {
                bool changed = false;
                char prev = trend[n >> 6];
                for (int j = 1; i < 6; i++) {
                    if (trend[n >> j] != prev) {
                        changed = true;
                    }
                }
                if (!changed) break;
            }
        }
        std::cout << "Trend: ";
        for (const auto &e : trend) {
            std::cout << "Stride " << e.first << " pattern " << e.second << "\n";
        }
    }
}