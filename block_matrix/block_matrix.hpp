#include <cstdint>
#include <stdexcept>
#include <unistd.h>
#include <errno.h>
#include <iostream>
#include <algorithm>
#include <thread>

const uint32_t CACHE_LINE_SIZE = 64;

class matrix
{
    uint64_t columns_;
    uint64_t rows_;
    double* data_; // place for optimization data_[]
public:
     matrix(uint64_t col, uint64_t row, double def);
     matrix(uint64_t col, uint64_t row);
     explicit matrix(const char* file_name);
     matrix(const matrix& original);
     matrix(matrix&& original);
     matrix() = delete;

     ~matrix();
     void save(const char* file_name);
     matrix block_mult(matrix &B, long int num_threads);
};
