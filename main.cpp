#include <iostream>
#include <random>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <map>
#include <pthread.h>

constexpr int MAX_A = 1000;
constexpr long long MAX_S = 1 << 21;
constexpr long long MAX_M = 1 << 30;
constexpr int N_READS = 1'000'000;

uint64_t *mem = static_cast<uint64_t *>(mmap(nullptr, MAX_M, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));

void read(uint64_t *p, long spots) {
    for (int i = 0; i < N_READS; i++) {
        p = (uint64_t *) *p;
        asm volatile("" : "+r"(p));
    }
}

long long time(long spots, int stride) {
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
    return ct != 0 && (nt / ct) >= 1.3;
}

std::map<long long, long long> strideToJump;

bool isMovement(long long st1, long long st2) {
    if (!strideToJump.contains(st2) || !strideToJump.contains(st1)) return true;
    return strideToJump[st1] != strideToJump[st2];
}

void printJumps() {
    for (const auto &[fst, snd]: strideToJump) {
        std::cout << "Stride " << fst << ": " << snd << "\n";
    }
}


int main() {
    int s = 1, n = 1 << 6;
    long long current_time = 0;
    while (s * n < MAX_M) {
        s = 1;
        while (s < MAX_A) {
            long long new_time = time(s, n);
            std::cout << "s " << s << " n " << n << " time: " << new_time << "\n";
            if (jump(current_time, new_time)) {
                strideToJump[n] = s;
            }
            s++;
            current_time = new_time;
        }
        if (isMovement(n, n / 2)) {
            n <<= 1;
            continue;
        }
        break;
    }
    printJumps();
}