//
// Created by jiakunyan on 1/30/21.
//

#ifndef FABRICBENCH_COMM_EXP_HPP
#define FABRICBENCH_COMM_EXP_HPP
#include <iostream>
#include <sys/time.h>
#include "config.hpp"
#include "thread_utils.hpp"
#include <vector>
#include <math.h>
#include <numeric> // std::adjacent_difference
#include <algorithm>

extern std::vector<std::vector<double>> checkpointTimesAll;
extern std::vector<std::vector<double>> checkpointTimesAllSkip;
extern std::vector<double> totalExecTimes;
extern omp_lock_t writelock;

void prepost_recv(int thread_id);

namespace fb {
static inline void comm_init() {
    MLOG_Init();
    pmi_master_init();
}

static inline void comm_free() {
    pmi_barrier();
    pmi_finalize();
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

template<typename FUNC>
static inline void RUN_VARY_MSG(std::pair<size_t, size_t> &&range,
                                const int report,
                                FUNC &&f, std::pair<int, int> &&iter = {0, 1}) {
    double t = 0;
    double t_skip = 0;
    double t_finish_thread = 0;
    // 20m messages
    int loop = TOTAL * 500;
    // 40k messages
    //int loop = TOTAL;
    int skip = 10240;
    std::vector<double> checkpointTimesThread;
    std::vector<double> checkpointTimesSkipThread;

    int checkpointStep = 1000;
    int numCheckPointsSkipPerWorker = ceil(skip / (omp::thread_count() * checkpointStep));
    int numCheckPointsPerWorker = ceil(loop / (omp::thread_count() * checkpointStep));

    checkpointTimesSkipThread.reserve(numCheckPointsSkipPerWorker);
    checkpointTimesThread.reserve(numCheckPointsPerWorker);

    int cnt = 0;
    for (size_t msg_size = range.first; msg_size <= range.second; msg_size <<= 1) {
//        if (msg_size >= LARGE) {
//            loop = TOTAL_LARGE;
//            skip = SKIP_LARGE;
//        }
//
        // prepost receives
        prepost_recv(omp::thread_id());
        //omp::proc_barrier();
        omp::thread_barrier();
        int messageIdx = 0;

        // start skip loop
        t_skip = wall_time();
        for (int i = iter.first; i < skip; i += iter.second) {
            f(msg_size, i);
            if (++cnt % checkpointStep == 0) {
                checkpointTimesSkipThread.push_back(wall_time() - t_skip);
            }
            //printf("thread %d finished skip %d\n", omp::thread_id(), messageIdx++);

        }
        //printf("skip done for thread %d, rank %d\n", omp::thread_id(), pmi_get_rank());
        cnt = 0;
//        prepost_recv(omp::thread_id());
//        //pmi_barrier();
//        // synchronize the threads between two processes
        //omp::proc_barrier();
        omp::thread_barrier();
//        if (omp::thread_id() == 0)
//        printf("skip done for all on rank %d!\n", pmi_get_rank());

        t = wall_time();
        messageIdx = 0;
        for (int i = iter.first; i < loop; i += iter.second) {
            f(msg_size, i);
            if (++cnt % checkpointStep == 0) {
                checkpointTimesThread.push_back(wall_time() - t);
            }
            //printf("thread %d finished loop %d\n", omp::thread_id(), messageIdx++);
        }
        //pmi_barrier();
        t_finish_thread = wall_time() - t;
        //printf("regular loop done for thread %d, rank %d\n", omp::thread_id(), pmi_get_rank());
        omp::thread_barrier();
//        if (omp::thread_id() == 0)
//            printf("regular loop done for all on rank %d!\n", pmi_get_rank());
        omp_set_lock(&writelock);
        totalExecTimes[omp::thread_id()] = t_finish_thread;
        checkpointTimesAllSkip[omp::thread_id()] = checkpointTimesSkipThread;
        checkpointTimesAll[omp::thread_id()] = checkpointTimesThread;
        omp_unset_lock(&writelock);
        omp::thread_barrier();
        t = *std::max_element(totalExecTimes.begin(), totalExecTimes.end());

        if (report) {
            double latency = 1e6 * get_latency(t, 2.0 * loop);
            double msgrate = get_msgrate(t, 2.0 * loop) / 1e6;
            double bw = get_bw(t, msg_size, 2.0 * loop) / 1024 / 1024;
            double completion_time_ms = t * 1e3;
            char output_str[256];
            int used = 0;
            used += snprintf(output_str + used, 256, "%-10lu %-10.2f %-10.3f %-10.2f %-10.2f %-10.2f %-10.2f %-10.2f",
                             omp::thread_count(), latency, msgrate, bw, completion_time_ms, 0, 0, 0);
            printf("%s\n", output_str);
            fflush(stdout);

            // csv header 1
            printf("thread_id,iter,time, is_skip\n");
            // print out per thread checkpoint times
            for (int id = 0; id < omp::thread_count(); id++) {
                std::vector<double> resultsSkip(numCheckPointsSkipPerWorker, 0);
                std::adjacent_difference(checkpointTimesAllSkip[id].begin(), checkpointTimesAllSkip[id].end(), resultsSkip.begin());
                for (int iter = 0; iter < resultsSkip.size(); iter++) {
                    printf("%d,%d,%.3f,%d\n", id, iter, resultsSkip[iter] * 1000, 1);
                }
                std::vector<double> results(numCheckPointsPerWorker, 0);
                std::adjacent_difference(checkpointTimesAll[id].begin(), checkpointTimesAll[id].end(), results.begin());
                for (int iter = 0; iter < results.size(); iter++) {
                    printf("%d,%d,%.3f,%d\n", id, iter, results[iter] * 1000, 0);
                }
            }
            // csv header 2
            printf("thread_id,total_time\n");
            for (int id = 0; id < omp::thread_count(); id++) {
                printf("%d,%f\n", id, iter, totalExecTimes[id] * 1000);
            }
        }
    }
    omp::proc_barrier();

    //pmi_barrier();

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
