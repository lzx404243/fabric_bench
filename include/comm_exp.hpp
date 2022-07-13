//
// Created by jiakunyan on 1/30/21.
//

#ifndef FABRICBENCH_COMM_EXP_HPP
#define FABRICBENCH_COMM_EXP_HPP
#include <iostream>
#include <sys/time.h>
#include "config.hpp"
#include "thread_utils.hpp"
#include <atomic>
#include <vector>
#include <math.h>
#include <numeric> // std::adjacent_difference
#include <algorithm>

int rx_thread_num = 1;
//extern fb::time_acc_t * compute_time_accs;
//extern fb::time_acc_t * idle_time_accs;
//extern fb::counter_t* progress_counters;


std::vector<std::vector<double>> checkpointTimesAll;
std::vector<std::vector<double>> checkpointTimesAllSkip;
std::vector<double> totalExecTimes;
omp_lock_t writelock;

void prepost_recv(int thread_id);
void reset_counters();

namespace fb {

struct alignas(64) time_acc_t {
    double tot_time_us = 0;
};

struct alignas(64) counter_t {
    long long count = 0;
};

struct sync_t {
    alignas(64) std::atomic<int> sync;
    char pad[64 - sizeof(std::atomic<int>)];
};

static inline void comm_init() {
    MLOG_Init();
    pmi_master_init();
}

static inline void comm_free() {
    pmi_barrier();
    pmi_finalize();
}

static inline void sleep_for_us(int compute_time_in_us, time_acc_t & time_acc) {
    struct timespec start, stop;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
    while (1) {
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop);
        double elapsed_us = ( stop.tv_nsec - start.tv_nsec ) / 1e3
                + ( stop.tv_sec - start.tv_sec ) * 1e6;
        if (elapsed_us >= compute_time_in_us) {
            //printf("time elapsed: %lf\n", elapsed_us);
            time_acc.tot_time_us += elapsed_us;
            break;
        }
    }
    return;
}

static inline void sleep_for_ns(int compute_time_in_ns) {
    struct timespec start, stop;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
    while (1) {
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &stop);
        double elapsed_us = ( stop.tv_nsec - start.tv_nsec)
                + ( stop.tv_sec - start.tv_sec ) * 1e9;
        if (elapsed_us >= compute_time_in_ns) {
            break;
        }
    }
    return;
}

static inline double wall_time() {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return t1.tv_sec + t1.tv_nsec / 1e9;
}

void write_buffer(char *buffer, int len, char input) {
    for (int i = 0; i < len; ++i) {
        buffer[i] = input;
    }
}

void check_buffer(const char *buffer, int len, char expect) {
    for (int i = 0; i < len; ++i) {
        if (buffer[i] != expect) {
            printf("check_buffer failed! buffer[%d](%d) != %d. ABORT!\n", i, buffer[i], expect);
            abort();
        }
    }
}

static inline double get_latency(double time, double n_msg) {
    return time / n_msg;
}

static inline double get_msgrate(double time, double n_msg) {
    return n_msg / time;
}

static inline double get_bw(double time, size_t size, double n_msg) {
    return n_msg * size / time;
}

static inline double get_overhead(time_acc_t * thread_acc_times) {
    double total_time_us = 0;

    for (int i = 0; i < omp::thread_count(); i++) {
        total_time_us += thread_acc_times[i].tot_time_us;
    }
    double average_time_ms = (total_time_us / omp::thread_count()) / 1e3;
    // return value in ms
    return average_time_ms;
}

static inline long long get_progress_total(const counter_t* progress_counters) {
    long long total = 0;
    for (int i = 0; i < rx_thread_num; i++) {
        total += progress_counters[i].count;
    }
    return total;
}

template<typename FUNC>
static inline void RUN_VARY_MSG(std::pair<size_t, size_t> &&range,
                                const int report,
                                FUNC &&f, std::pair<int, int> &&iter = {0, 1}) {
    double t = 0;
    int loop = TOTAL;
    int skip = SKIP;

    for (size_t msg_size = range.first; msg_size <= range.second; msg_size <<= 1) {
        if (msg_size >= LARGE) {
            loop = TOTAL_LARGE;
            skip = SKIP_LARGE;
        }
        // prepost receives
        prepost_recv(omp::thread_id());
        omp::proc_barrier();
        // warm up loop
        for (int i = iter.first; i < skip; i += iter.second) {
            f(msg_size, i);
        }
        omp::proc_barrier();
        if (omp::thread_id() == 0) {
            // one thread is resetting syncs, counters etc for all workers
            reset_counters();
        }
        omp::proc_barrier();
        t = wall_time();
        //int counter = 0;
        for (int i = iter.first; i < loop; i += iter.second) {
            f(msg_size, i);
            //printf("thread %d -- rank %d finishes iter %d\n", omp::thread_id(), pmi_get_rank(), counter++);
        }
        //printf("thread %d -- rank %d done with all\n", omp::thread_id(), pmi_get_rank());
        omp::thread_barrier();
        t = wall_time() - t;

        if (report) {
            //printReport();
            double latency = 1e6 * get_latency(t, 2.0 * loop);
            double msgrate = get_msgrate(t, 2.0 * loop) / 1e6;
            double bw = get_bw(t, msg_size, 2.0 * loop) / 1024 / 1024;
            double completion_time_ms = t * 1e3;
//            double compute_time_ms = get_overhead(compute_time_accs);
//            long long progress_counter_sum = get_progress_total(progress_counters);
//            double idle_time_ms = get_overhead(idle_time_accs);

            char output_str[256];
            int used = 0;
            used += snprintf(output_str + used, 256, "%-10lu %-10.2f %-10.3f %-10.2f %-10.2f %-10.2f %-10.2f %-10.2f",
                             omp::thread_count() + rx_thread_num, latency, msgrate, bw, completion_time_ms, 0, 0, 0);
            printf("%s\n", output_str);
            fflush(stdout);
        }
    }
    omp::proc_barrier();
}

inline int comm_set_me_to(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}
} // namespace fb
#endif//FABRICBENCH_COMM_EXP_HPP
