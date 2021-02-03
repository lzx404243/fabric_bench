#ifndef FABRICBENCH_THREAD_UTILS_HPP
#define FABRICBENCH_THREAD_UTILS_HPP

#include <omp.h>
#include <unistd.h>
#include "mlog.h"

namespace omp {

typedef void* (*func_t)(void*);

static inline int thread_id()
{
  return omp_get_thread_num();
}

static inline int thread_count()
{
  return omp_get_num_threads();
}

static inline void thread_barrier()
{
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "Thread %d enter barrier.\n", thread_id());
  #pragma omp barrier
    MLOG_DBG_Log(MLOG_LOG_DEBUG, "Thread %d leave barrier.\n", thread_id());
}

static inline void thread_run(func_t f, int n)
{
#pragma omp parallel num_threads(n)
    {
        char hostname[256];
        gethostname(hostname, 256);
        MLOG_Log(MLOG_LOG_TRACE, "Process %2d/%2d thread %2d/%2d on cpu %2d host %s start\n", pmi_get_rank(), pmi_get_size(), thread_id(), thread_count(), sched_getcpu(), hostname);
        f((void*)0);
        MLOG_Log(MLOG_LOG_TRACE, "Process %2d/%2d thread %2d/%2d on cpu %2d host %s exit\n", pmi_get_rank(), pmi_get_size(), thread_id(), thread_count(), sched_getcpu(), hostname);
    }
}

}
#endif