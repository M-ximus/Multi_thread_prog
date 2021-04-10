#include "block_matrix.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <chrono>
#include <errno.h>

int main(int argc, char* argv[])
{
    if (argc != 5)
    {
        printf("Bad number of input arguments\n");
        printf("Try ./mul A.matr B.matr out.matr num_threads\n");
        exit(EXIT_FAILURE);
    }

    errno = 0;
    long int num_threads = strtol(argv[4], NULL, 10);
    if (errno != 0)
    {
        perror("Getting threads number return error\n");
        exit(EXIT_FAILURE);
    }

    try
    {
        matrix A(argv[1]);
        matrix B(argv[2]);

        auto start = std::chrono::high_resolution_clock::now();
        matrix C = A.block_mult(B, num_threads);
        auto end   = std::chrono::high_resolution_clock::now();

        std::chrono::duration<double> exec_time = end - start;
        std::cout << "Execution time: " << exec_time.count() << "\n";

        C.save(argv[3]);
    }
    catch (std::exception &error)
    {
        std::cout << error.what();
        exit(EXIT_FAILURE);
    }

    return 0;
}
