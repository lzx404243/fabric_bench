#include "bench_fabric.hpp"
#include "thread_utils.hpp"
#include "comm_exp.hpp"
#include "unistd.h"

using namespace fb;

int thread_num = 1;
int rank, size;
device_t device;

void* hello_world(void* arg) {
    int thread_id = omp::thread_id();
    int thread_count = omp::thread_count();
    char hostname[256];
    gethostname(hostname, 256);

    printf("Hello! I am process %2d/%2d thread %2d/%2d on cpu %2d host %s\n", rank, size, thread_id, thread_count, sched_getcpu(), hostname);

    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc > 1)
        thread_num = atoi(argv[1]);

    comm_init();
    init_device(&device, thread_num != 1);
    rank = pmi_get_rank();
    size = pmi_get_size();
    omp::thread_run(hello_world, thread_num);

    free_device(&device);
    comm_free();
    return 0;
}


