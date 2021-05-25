#include "stack.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <iostream>

const size_t NUM_ITERS = 2000000;

struct thread_info
{
    size_t                  num_iters_;
    lockfree_stack<size_t>* stack_ptr_;
    double                  result_;
} __attribute__((aligned(64)));

void thread_routine(thread_info* data)
{
    if (data == nullptr)
        throw std::runtime_error("[thread_routine] Input data array is nullptr\n");

    size_t stop_val = data->num_iters_;
    lockfree_stack<size_t>* stack = data->stack_ptr_;

    struct timespec start, end;
    clock_gettime(CLOCK_REALTIME, &start);

    for (size_t i = 0; i < stop_val; i++)
    {
        stack->push(i);
        stack->pop();
    }

    clock_gettime(CLOCK_REALTIME, &end);

    long seconds  = end.tv_sec - start.tv_sec;
    long nanosec  = end.tv_nsec - start.tv_nsec;
    data->result_ = seconds + nanosec * 1e-9;
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Bad input comand line\n");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    long num_threads = strtol(argv[1], NULL, 10);
    if (errno < 0)
    {
        perror("get num treads returned error");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    thread_info* arr_info = (thread_info*) aligned_alloc(64, sizeof(*arr_info) * num_threads);
    if (arr_info == nullptr)
    {
        perror("alloc aligned array with thread info returned error\n");
        exit(EXIT_FAILURE);
    }

    init_hazard_pointers_environment(num_threads);
    lockfree_stack<size_t> stack;

    size_t thread_task = NUM_ITERS / num_threads;
    for (long i = 0; i < num_threads; i++)
    {
        arr_info[i].num_iters_ = thread_task;
        arr_info[i].stack_ptr_ = &stack;
        arr_info[i].result_ = 0.0;
    }

    try
    {
        std::thread* arr_threads = new std::thread[num_threads];
        for (long i = 0; i < num_threads; i++)
            arr_threads[i] = std::thread(thread_routine, &arr_info[i]);

        for (long i = 0; i < num_threads; i++)
            arr_threads[i].join();

        delete[] arr_threads;
    }
    catch(const std::exception& error)
    {
        std::cerr << error.what();
        free(arr_info);
        destr_hazard_pointers_environment();
        exit(EXIT_FAILURE);
    }

    destr_hazard_pointers_environment();

    double max_time = 0;
    double min_time = NUM_ITERS;
    double sum = 0.0;

    for (long i = 0; i < num_threads; i++)
    {
        if (max_time < arr_info[i].result_)
            max_time = arr_info[i].result_;
        if (min_time > arr_info[i].result_)
            min_time = arr_info[i].result_;

        sum += arr_info[i].result_;
    }
    double average_per_instr = sum / ((double)(2 * NUM_ITERS));

    printf("average = %lg, min = %lg, max = %lg\n", average_per_instr, min_time, max_time);

    free(arr_info);

    return 0;
}
