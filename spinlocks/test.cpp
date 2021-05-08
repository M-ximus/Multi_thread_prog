#include "spinlocks.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <errno.h>
#include <exception>
#include <iostream>
#include <time.h>

#ifdef SPEED
#define TEST(lock, num_threads) do{\
    double res = speed_test((lock), (num_threads));\
    if (res < 0.0)\
    {\
        fprintf(stderr, "[SPEED_TEST] Bad speed test\n");\
        exit(EXIT_FAILURE);\
    }\
    else\
    {\
        printf("[SPEED_TEST] time = %lg\n", res);\
    }\
} while(0);
#else
#define TEST(lock, num_threads) do{\
    if (!correctness_test((lock), (num_threads)))\
    {\
        fprintf(stderr, "[CORR_TEST] FAILED: Bad correctness test\n");\
        exit(EXIT_FAILURE);\
    }\
    else\
    {\
        printf("[CORR_TEST] PASSED\n");\
    }\
} while(0);
#endif

static const size_t NUM_THREADS = 8;
static const size_t CORR_NUM_COUNTS  = 1000000;
static const size_t SPEED_NUM_COUNTS = 0x400000; //2^16

int correctness_test(spinlock& sinc, size_t num_threads);
double speed_test(spinlock& sinc, size_t num_threads);

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "[main] Bad number of input arguments\n");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    long int num_test_threads = strtol(argv[2], NULL, 10);
    if (errno < 0)
    {
        perror("[main] Num threads is error number\n");
        exit(EXIT_FAILURE);
    }

    if (!strncmp(argv[1], "TAS", 4))
    {
        spinlock_TAS sinc;
        TEST(sinc, num_test_threads);
    }
    else if (!strncmp(argv[1], "TTAS", 5))
    {
        spinlock_TTAS sinc;
        TEST(sinc, num_test_threads);
    }
    else if (!strncmp(argv[1], "ticket", 7))
    {
        ticket_lock sinc;
        TEST(sinc, num_test_threads);
    }
    else
    {
        printf("Bad input parameter\n. Try TAS, TTAS or ticket\n");
        return 0;
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////

struct thread_data_corr
{
    size_t*   counter;
    spinlock* sync_mech;
    size_t    limit;
} __attribute__((aligned(64))); // cache_line alignment

void correctness_thread_routine(thread_data_corr* data)
{
    if (data == nullptr)
    {
        fprintf(stderr, "[correctness_thread_routine] input data return error\n");
        return;
    }

    size_t num_cycles_for_thread = data->limit;
    spinlock* sync = data->sync_mech;
    size_t* shared = data->counter;

    assert(sync != nullptr);
    assert(shared != nullptr);

    for (size_t i = 0; i < num_cycles_for_thread; i++)
    {
        sync->lock();
        (*shared)++;
        sync->unlock();
    }
}

int correctness_test(spinlock& sync, size_t num_threads)
{
    size_t num_counts  = CORR_NUM_COUNTS;

    errno = 0;
    thread_data_corr* data = (thread_data_corr*) aligned_alloc(64, sizeof(*data) * num_threads);
    if (data == nullptr)
    {
        perror("[correctness_test] aligned alloc of threads data returned error\n");
        throw std::runtime_error("[correctness_test] aligned alloc\n");
    }

    size_t shared_counter = 0;
    for (size_t i = 0; i < num_threads; i++)
    {
        data[i].counter   = &shared_counter;
        data[i].sync_mech = &sync; // not copy
        data[i].limit     = num_counts;
    }

    try
    {
        std::thread* arr_threads = new std::thread[num_threads];

        for (size_t i = 0; i < num_threads; i++)
            arr_threads[i] = std::thread(correctness_thread_routine, &data[i]);

        for (size_t i = 0; i < num_threads; i++)
            arr_threads[i].join();

        delete[] arr_threads;
    }
    catch(const std::exception& error)
    {
        fprintf(stderr, "[correctness_test] alloc, start and join threads throw exception\n");
        free(data);
        throw error;
    }

    free(data);

    if (shared_counter != num_threads * num_counts)
        return 0;

    return 1; // TRUE
}

////////////////////////////////////////////////////////////////////////////////

struct thread_data_speed
{
    size_t limits;
    size_t local_counter;
    double cpu_time;
    spinlock* sync_mech;
} __attribute__ ((aligned(64)));

void speed_thread_routine(thread_data_speed* data)
{
    if (data == nullptr)
    {
        fprintf(stderr, "[speed_thread_routine] input data is null\n");
        return;
    }

    if (data->sync_mech == nullptr)
    {
        fprintf(stderr, "[speed_thread_routine] input syncronization method is null\n");
        return;
    }

    spinlock* sync = data->sync_mech;
    size_t num_cycles = data->limits;

    struct timespec start, end;
    for (size_t i = 0; i < num_cycles; i++)
    {
        sync->lock();
        (data->local_counter)++;
        sync->unlock();
        if (i == 0)
            clock_gettime(CLOCK_REALTIME, &start); // start time only after last thread was created
    }
    clock_gettime(CLOCK_REALTIME, &end);

    long seconds = end.tv_sec - start.tv_sec;
    long nanosec = end.tv_nsec - start.tv_nsec;
    data->cpu_time = seconds + nanosec * 1e-9;
}

double speed_test(spinlock& sync, size_t num_threads)
{
    size_t num_counts = SPEED_NUM_COUNTS;

    errno = 0;
    thread_data_speed* data = (thread_data_speed*) aligned_alloc(64, sizeof(*data) * num_threads);
    if (data == nullptr)
    {
        perror("[speed_test] can't aligned alloc thread data array\n");
        throw std::runtime_error("[speed_test] aligned_alloc\n");
    }

    size_t num_cycles_for_thread = num_counts / num_threads;

    for (size_t i = 0; i < num_threads; i++)
    {
        data[i].limits        = num_cycles_for_thread;
        data[i].local_counter = 0;
        data[i].cpu_time      = 0.0;
        data[i].sync_mech     = &sync;
    }

    double sum_time = 0.0;
    double min_time = num_cycles_for_thread;
    double max_time = 0.0;
    try
    {
        std::thread* threads_arr = new std::thread[num_threads];

        sync.lock(); // all threads must wait the last thread

        for (size_t i = 0; i < num_threads; i++)
            threads_arr[i] = std::thread(speed_thread_routine, &data[i]);

        sync.unlock(); // start computations

        for (size_t i = 0; i < num_threads; i++)
        {
            threads_arr[i].join();
            sum_time += data[i].cpu_time;

            if (max_time < data[i].cpu_time)
                max_time = data[i].cpu_time;

            if (min_time > data[i].cpu_time)
                min_time = data[i].cpu_time;
        }

        delete[] threads_arr;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "[speed_test] thread data alloc or stat/join threads error in try block\n");
        free(data);
        throw error;
    }

    double last_time    = data[num_threads - 1].cpu_time; // the last thread gives the most accurate time
    double average_time = sum_time / num_counts; // average time per 1 operation

    free(data);

    printf("average = %lg, sum = %lg, min = %lg, max = %lg, last_thread = %lg\n", average_time, sum_time, min_time, max_time, last_time);

    return sum_time;
}
