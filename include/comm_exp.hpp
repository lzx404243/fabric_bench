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


extern int rx_thread_num;
extern std::atomic<int> thread_started;

namespace fb {
static inline void comm_init() {
    MLOG_Init();
    pmi_master_init();
}

static inline void comm_free() {
    pmi_barrier();
    pmi_finalize();
}

static inline void sleep_for_us(int compute_time_in_us) {
    struct timespec start, stop;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &stop);
        if ((stop.tv_nsec - start.tv_nsec) * 1e3 >= compute_time_in_us) {
            break;
        }
    }
    return;
}

static inline double wtime() {
    timeval t1;
    gettimeofday(&t1, nullptr);
    return t1.tv_sec + t1.tv_usec / 1e6;
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

template<typename FUNC>
static inline void RUN_VARY_MSG(std::pair<size_t, size_t> &&range,
                                const int report,
                                FUNC &&f, std::pair<int, int> &&iter = {0, 1}) {
    double t = 0;

    int loop = TOTAL;
    int skip = SKIP;

    for (size_t msg_size = range.first; msg_size <= range.second; msg_size <<= 1) {
//        if (msg_size >= LARGE) {
//            loop = TOTAL_LARGE;
//            skip = SKIP_LARGE;
//        }
        for (int i = iter.first; i < skip; i += iter.second) {
            f(msg_size, i);
        }
        omp::thread_barrier();
        // wait for the progress thread to set up again.
        while (thread_started.load() != rx_thread_num) continue;
        omp::thread_barrier();
        //pmi_barrier();
        t = wtime();

        for (int i = iter.first; i < loop; i += iter.second) {
            f(msg_size, i);
        }
        //pmi_barrier();
        //printf("ranks %i, thread %i done!\n", pmi_get_rank(), omp::thread_id());

        omp::thread_barrier();
        t = wtime() - t;
        //printf("all ranks done!\n");

        if (report) {
            double latency = 1e6 * get_latency(t, 2.0 * loop);
            double msgrate = get_msgrate(t, 2.0 * loop) / 1e6;
            double bw = get_bw(t, msg_size, 2.0 * loop) / 1024 / 1024;

            char output_str[256];
            int used = 0;
            // output is modified to show the worker thread
            used += snprintf(output_str + used, 256, "%-10lu %-10.2f %-10.3f %-10.2f %-10.2f",
                             omp::thread_count() + rx_thread_num, latency, msgrate, bw, t);
            printf("%s\n", output_str);
            fflush(stdout);
        }
    }
    //pmi_barrier();

    omp::thread_barrier();
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
