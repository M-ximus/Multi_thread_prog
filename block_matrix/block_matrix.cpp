#include "block_matrix.hpp"
#include <assert.h>

enum errors{
    E_SUCCESS  = 0,
    E_ERROR    = -1,
    E_BADALLOC = -2,
    E_OPEN     = -3,
};

struct block
{
    int cols_;
    int rows_;
    double* data_;
} __attribute__((aligned(64)));

struct matrix_of_blocks
{
    uint64_t cols_;
    uint64_t rows_;
    block* matrix_;
    double*  data_;
} __attribute__((aligned(64)));

static int read_matr_from_file(const char* file_name, double** buff, uint64_t* buff_rows, uint64_t* buff_cols)
{
    errno = 0;

    FILE* input = fopen(file_name, "r");
    if (input == nullptr)
    {
        perror("[read_matr_from_file] open file errror\n");
        return E_OPEN;
    }

    uint64_t num_rows = 0;
    uint64_t num_cols = 0;
    int ret = fscanf(input, "%ld\n%ld\n", &num_rows, &num_cols);
    if (ret != 2)
    {
        fprintf(stderr, "[read_matr_from_file] fscanf can't read number of rows/columns\n");
        return E_ERROR;
    }

    double* data = nullptr;
    try
    {
        data = new double[num_rows * num_cols];
    }
    catch (std::exception& error)
    {
        fprintf(stderr, "[read_matr_from_file] Bad alloc array for data\n");
        std::cerr << error.what();
        return E_BADALLOC;
    }

    for (uint64_t row = 0; row < num_rows; row++)
    {
        for (uint64_t col = 0; col < num_cols; col++)
        {
            int ret = fscanf(input, "%lg", &data[row * num_cols + col]);
            if (ret != 1)
            {
                fprintf(stderr, "Bad input matrix format\n");
                delete[] data;
                return E_ERROR;
            }
        }
    }

    fclose(input);

    *buff      = data;
    *buff_rows = num_rows;
    *buff_cols = num_cols;

    return E_SUCCESS;
}

matrix::matrix(uint64_t col, uint64_t row, double def):
    columns_(col),
    rows_(row)
{
    data_ = new double[rows_ * columns_];
    std::fill_n(data_, rows_ * columns_, def);
}

matrix::matrix(uint64_t col, uint64_t row):
    columns_(col),
    rows_(row)
{
    data_ = new double[rows_ * columns_];
}

matrix::matrix(const char* file_name)
{
    if (file_name == nullptr)
        throw std::invalid_argument("[matrix::matrix] Bad pointer to file_name\n");

    double* buffer = nullptr;
    uint64_t buffer_rows = 0;
    uint64_t buffer_cols = 0;
    int err = read_matr_from_file(file_name, &buffer, &buffer_rows, &buffer_cols);
    if (err != E_SUCCESS)
        throw std::runtime_error("[matrix::matrix] Read_file return " + std::to_string(err));

    data_    = buffer;
    rows_    = buffer_rows;
    columns_ = buffer_cols;
}

matrix::matrix(const matrix& original)
{
    std::cout << "Copy constructor called\n";
    printf("copy\n");

    columns_ = original.columns_;
    rows_    = original.rows_;

    uint64_t size = rows_ * columns_;
    data_ = new double[size];

    for (uint64_t i = 0; i < size; i++)
        data_[i] = original.data_[i];
}

matrix::matrix(matrix&& original)
{
    std::cout << "Move constructor called\n";
    printf("move\n");

    rows_    = original.rows_;
    columns_ = original.columns_;

    data_ = original.data_;

    original.data_ = nullptr;
}

matrix::~matrix()
{
    std::cout << "destructor called\n";
    delete[] data_;
}

void matrix::save(const char* file_name)
{
    if (file_name == nullptr)
        throw std::invalid_argument("[matrix::save] File pointer to save is nullptr\n");

    errno = 0;
    FILE* output = fopen(file_name, "w");
    if (output == nullptr)
    {
        perror("[matrix::save] fopen file error\n");
        throw std::runtime_error("[matrix::save] save file can't open file\n");
    }

    fprintf(output, "%ld\n%ld\n", rows_, columns_);

    for (uint64_t row = 0; row < rows_; row++)
    {
        for (uint64_t col = 0; col < columns_; col++)
            if (col != columns_ - 1)
                fprintf(output, "%lg ", data_[row * columns_ + col]);
            else
                fprintf(output, "%lg\n", data_[row * columns_ + col]);
    }

    fclose(output);

    return;
}

static matrix_of_blocks* produce_block_matrix(uint64_t num_cols, uint64_t num_rows, double* matrix)
{
    //assert(matrix != nullptr);

    uint64_t block_dim =  CACHE_LINE_SIZE / sizeof(double);
    uint64_t num_block_norm_cols = num_cols / block_dim; // cache line / sizeof(double)
    uint64_t num_block_norm_rows = num_rows / block_dim;
    uint64_t dim_block_rem_cols  = num_cols % block_dim;
    uint64_t dim_block_rem_rows  = num_rows % block_dim;


    uint64_t all_block_cols = num_block_norm_cols;
    if (dim_block_rem_cols != 0)
        all_block_cols++;

    uint64_t all_block_rows = num_block_norm_rows;
    if (dim_block_rem_rows != 0)
        all_block_rows++;

    errno = 0;
    // norm + 1 -> norm + small remaining
    block* block_matrix = (block*)aligned_alloc(CACHE_LINE_SIZE, all_block_cols * all_block_rows * sizeof(*block_matrix));
    if (block_matrix == nullptr)
    {
        perror("[produce_block_matrix] aligned_alloc return error\n");
        return nullptr;
    }

    uint64_t block_size = block_dim * block_dim * sizeof(double);
    double* all_data = (double*)aligned_alloc(CACHE_LINE_SIZE, block_size * all_block_cols * all_block_rows);
    if (all_data == nullptr)
    {
        perror("[produce_block_matrix] aligned_alloc arr for all data\n");
        free(block_matrix);
        return nullptr;
    }

    matrix_of_blocks* result_matrix = (matrix_of_blocks*)aligned_alloc(CACHE_LINE_SIZE, sizeof(*result_matrix));
    if (result_matrix == nullptr)
    {
        perror("[produce_block_matrix] aligned_alloc arr for all data\n");
        free(block_matrix);
        free(all_data);
        return nullptr;
    }

    result_matrix->rows_   = all_block_rows;
    result_matrix->cols_   = all_block_cols;
    result_matrix->data_   = all_data;
    result_matrix->matrix_ = block_matrix;

    for (uint64_t row = 0; row < all_block_rows; row++)
    {
        for (uint64_t col = 0; col < all_block_cols; col++)
        {
            uint64_t offset       = row * block_dim * num_cols + col * block_dim;
            uint64_t block_offset = (row * all_block_cols + col) * block_dim * block_dim;

            //printf("offset = %ld, block_offset = %ld\n", offset, block_offset);

            if (col == all_block_cols - 1 && dim_block_rem_cols != 0)
                block_matrix[row * all_block_cols + col].cols_ = dim_block_rem_cols;
            else
                block_matrix[row * all_block_cols + col].cols_ = block_dim;

            if (row == all_block_cols - 1 && dim_block_rem_rows != 0)
                block_matrix[row * all_block_cols + col].rows_ = dim_block_rem_rows;
            else
                block_matrix[row * all_block_cols + col].rows_ = block_dim;

            block_matrix[row * all_block_cols + col].data_     = all_data + block_offset;

            //if matrix == NULL produce zero-filled matrix
            if (matrix == nullptr)
            {
                for (uint64_t orig_row = 0; orig_row < block_dim; orig_row++)
                    for (uint64_t orig_col = 0; orig_col < block_dim; orig_col++)
                        all_data[block_offset + orig_row * block_dim + orig_col] = 0.0;
            }
            else
            {
                for (uint64_t orig_row = 0; orig_row < block_matrix[row * all_block_cols + col].rows_; orig_row++)
                    for (uint64_t orig_col = 0; orig_col < block_matrix[row * all_block_cols + col].cols_; orig_col++)
                        block_matrix[row * all_block_cols + col].data_[orig_row * block_dim + orig_col] = matrix[offset + orig_row * num_cols + orig_col]; // need paint

                if (block_matrix[row * all_block_cols + col].rows_ != block_dim)
                {
                    //printf("Row - Block : row %ld, col %ld\nrow_dim = %d, col_dim = %d\n", row, col, block_matrix[row * all_block_cols + col].rows_, block_matrix[row * all_block_cols + col].cols_);
                    for (uint64_t orig_row = block_matrix[row * all_block_cols + col].rows_; orig_row < block_dim; orig_row++)
                        for (uint64_t orig_col = 0; orig_col < block_dim; orig_col++)
                            block_matrix[row * all_block_cols + col].data_[orig_row * block_dim + block_dim - 1] = 0.0; // need paint
                }
                if (block_matrix[row * all_block_cols + col].cols_ != block_dim)
                {
                    //printf("Col - Block : row %ld, col %ld\nrow_dim = %d, col_dim = %d\n", row, col, block_matrix[row * all_block_cols + col].rows_, block_matrix[row * all_block_cols + col].cols_);
                    for (uint64_t orig_col = block_matrix[row * all_block_cols + col].cols_; orig_col < block_dim; orig_col++)
                        for (uint64_t orig_row = 0; orig_row < block_dim; orig_row++)
                            block_matrix[row * all_block_cols + col].data_[orig_row * block_dim + orig_col] = 0.0; // need paint
                }
            }
        }
    }

    return result_matrix;
}

static matrix_of_blocks* produce_trans_block(uint64_t num_cols, uint64_t num_rows, double* matrix)
{
    //assert(matrix != nullptr);

    uint64_t block_dim =  CACHE_LINE_SIZE / sizeof(double);
    uint64_t num_block_norm_cols = num_rows / block_dim; // transpose size
    uint64_t num_block_norm_rows = num_cols / block_dim;
    uint64_t dim_block_rem_cols  = num_rows % block_dim;
    uint64_t dim_block_rem_rows  = num_cols % block_dim;

    uint64_t all_block_cols = num_block_norm_cols;
    if (dim_block_rem_cols != 0)
        all_block_cols++;

    uint64_t all_block_rows = num_block_norm_rows;
    if (dim_block_rem_rows != 0)
        all_block_rows++;

    //printf("%ld, %ld\n", all_block_rows, all_block_cols);

    errno = 0;
    // norm + 1 -> norm + small remaining
    block* block_matrix = (block*)aligned_alloc(CACHE_LINE_SIZE, all_block_cols * all_block_rows * sizeof(*block_matrix));
    if (block_matrix == nullptr)
    {
        perror("[produce_trans_block] aligned_alloc return error\n");
        return nullptr;
    }

    uint64_t block_size = block_dim * block_dim * sizeof(double);
    double* all_data = (double*)aligned_alloc(CACHE_LINE_SIZE, block_size * all_block_cols * all_block_rows);
    if (all_data == nullptr)
    {
        perror("[produce_trans_block] aligned_alloc arr for all data\n");
        free(block_matrix);
        return nullptr;
    }

    matrix_of_blocks* result_matrix = (matrix_of_blocks*)aligned_alloc(CACHE_LINE_SIZE, sizeof(*result_matrix));
    if (result_matrix == nullptr)
    {
        perror("[produce_trans_block] aligned_alloc arr for all data\n");
        free(block_matrix);
        free(all_data);
        return nullptr;
    }

    result_matrix->rows_   = all_block_rows;
    result_matrix->cols_   = all_block_cols;
    result_matrix->data_   = all_data;
    result_matrix->matrix_ = block_matrix;

    for (uint64_t row = 0; row < all_block_rows; row++)
    {
        for (uint64_t col = 0; col < all_block_cols; col++)
        {
            uint64_t offset       = col * num_cols * block_dim + row * block_dim;
            uint64_t block_offset = (row * all_block_cols + col) * block_dim * block_dim;

            if (col == all_block_cols - 1 && dim_block_rem_cols != 0)
                block_matrix[row * all_block_cols + col].cols_ = dim_block_rem_cols;
            else
                block_matrix[row * all_block_cols + col].cols_ = block_dim;

            if (row == all_block_rows - 1 && dim_block_rem_rows != 0)
                block_matrix[row * all_block_cols + col].rows_ = dim_block_rem_rows;
            else
                block_matrix[row * all_block_cols + col].rows_ = block_dim;

            block_matrix[row * all_block_cols + col].data_     = all_data + block_offset;

            //printf("Block : row %ld, col %ld\nrow_dim = %d, col_dim = %d\n", row, col, block_matrix[row * all_block_cols + col].rows_, block_matrix[row * all_block_cols + col].cols_);
            //printf("offset = %ld, block_offset = %ld\n", offset, block_offset);

            //if matrix == NULL produce zero-filled matrix
            if (matrix == nullptr)
            {
                for (uint64_t orig_row = 0; orig_row < block_dim; orig_row++)
                    for (uint64_t orig_col = 0; orig_col < block_dim; orig_col++)
                        all_data[block_offset + orig_row * block_dim + orig_col] = 0.0;
            }
            else
            {
                for (uint64_t orig_row = 0; orig_row < block_matrix[row * all_block_cols + col].rows_; orig_row++)
                    for (uint64_t orig_col = 0; orig_col < block_matrix[row * all_block_cols + col].cols_; orig_col++)
                        block_matrix[row * all_block_cols + col].data_[orig_row * block_dim + orig_col] = matrix[offset + orig_col * num_cols + orig_row]; // need paint

                if (block_matrix[row * all_block_cols + col].rows_ != block_dim)
                {
                    //printf("Row - Block : row %ld, col %ld\nrow_dim = %d, col_dim = %d\n", row, col, block_matrix[row * all_block_cols + col].rows_, block_matrix[row * all_block_cols + col].cols_);
                    for (uint64_t orig_row = block_matrix[row * all_block_cols + col].rows_; orig_row < block_dim; orig_row++)
                        for (uint64_t orig_col = 0; orig_col < block_dim; orig_col++)
                            block_matrix[row * all_block_cols + col].data_[orig_row * block_dim + block_dim - 1] = 0.0; // need paint
                }
                if (block_matrix[row * all_block_cols + col].cols_ != block_dim)
                {
                    //printf("Col - Block : row %ld, col %ld\nrow_dim = %d, col_dim = %d\n", row, col, block_matrix[row * all_block_cols + col].rows_, block_matrix[row * all_block_cols + col].cols_);
                    for (uint64_t orig_col = block_matrix[row * all_block_cols + col].cols_; orig_col < block_dim; orig_col++)
                        for (uint64_t orig_row = 0; orig_row < block_dim; orig_row++)
                            block_matrix[row * all_block_cols + col].data_[orig_row * block_dim + orig_col] = 0.0; // need paint
                }
            }
        }
    }

    return result_matrix;
}

static void debug_print_block_matrix(matrix_of_blocks* matr, const char* debug_file)
{
    assert(matr != nullptr);
    assert(debug_file != nullptr);

    errno = 0;
    FILE* out_file = fopen(debug_file, "w");
    if (out_file == nullptr)
    {
        perror("[debug_print_block_matrix] can't open debug_file\n");
        return;
    }

    uint64_t num_cols = matr->cols_;
    uint64_t num_rows = matr->rows_;
    int block_dim = matr->matrix_[0].cols_;

    fprintf(out_file, "num_rows = %ld\nnum_cols = %ld\n", num_rows, num_cols);

    for (uint64_t num_block = 0; num_block < num_cols * num_rows; num_block++)
    {
        fprintf(out_file, "Block %ld\n", num_block);
        for (int row = 0; row < block_dim; row++)
        {
            for (int col = 0; col < block_dim; col++)
                fprintf(out_file, "%lg ", matr->matrix_[num_block].data_[row * block_dim + col]);
            fprintf(out_file, "\n");
        }
    }
    fclose(out_file);
}

static void block_distruct(matrix_of_blocks* matr)
{
    assert(matr != nullptr);

    matr->cols_ = -1;
    matr->rows_ = -1;

    free(matr->data_);
    free(matr->matrix_);
}

struct thread_info
{
    uint64_t first_;
    uint64_t last_;

    matrix_of_blocks* normal_;
    matrix_of_blocks* transp_;
    matrix_of_blocks* return_;
} __attribute__((aligned(64)));

void thread_routine(thread_info* info)
{
    printf("thread_routine was started with diap: first = %ld, last = %ld\n", info->first_, info->last_);

    int block_dim = info->normal_->matrix_[0].cols_;

    uint64_t normal_rows = info->normal_->rows_;
    uint64_t normal_cols = info->normal_->cols_;
    block* A             = info->normal_->matrix_;

    uint64_t transp_rows = info->transp_->rows_;
    uint64_t transp_cols = info->transp_->cols_;
    block* B             = info->transp_->matrix_;

    //printf("A: row = %ld, col = %ld\n", normal_rows, normal_cols);
    //printf("B: row = %ld, col = %ld\n", transp_rows, transp_cols);

    block* C             = info->return_->matrix_;

    //printf("C: row = %ld, col = %ld\n", info->return_->rows_, info->return_->cols_);

    for (uint64_t cur_block = info->first_; cur_block < info->last_; cur_block++)
    {
        uint64_t block_row = cur_block / transp_rows;
        uint64_t block_col = cur_block % transp_rows;
        //printf("block_row = %ld, block_col = %ld\n", block_row, block_col);

        double* data_block = C[cur_block].data_;

        for (uint64_t i = 0; i < normal_cols; i++)
        {
            double* A_block = A[normal_cols * block_row + i].data_;
            double* B_block = B[normal_cols * block_col + i].data_;

            for (int row = 0; row < block_dim; row++)
                for (int col = 0; col < block_dim; col++)
                    for (int k = 0; k < block_dim; k++)
                        data_block[row * block_dim + col] += A_block[row * block_dim + k] * B_block[row * block_dim + k];
        }
    }
}

static int mult_prep_block_matr_multitread(matrix_of_blocks* A_normal, matrix_of_blocks* B_transp, matrix_of_blocks* C_return, long int num_threads)
{
    assert(A_normal != nullptr);
    assert(B_transp != nullptr);
    assert(C_return != nullptr);

    errno = 0;
    thread_info* arr_thread_info = (thread_info*)aligned_alloc(CACHE_LINE_SIZE, num_threads * sizeof(*arr_thread_info));
    if (arr_thread_info == nullptr)
    {
        perror("[mult_prep_block_matr_multitread] aligned allocation of thread_info array returned error\n");
        return E_BADALLOC;
    }

    uint64_t step = C_return->cols_ * C_return->rows_ / num_threads;
    for (long int i = 0; i < num_threads; i++)
    {
        arr_thread_info[i].first_ = i * step;
        arr_thread_info[i].last_ = arr_thread_info[i].first_ + step;

        arr_thread_info[i].normal_ = A_normal;
        arr_thread_info[i].transp_ = B_transp;
        arr_thread_info[i].return_ = C_return;
    }

    try
    {
        std::thread* arr_thread = new std::thread[num_threads];
        for (long int i = 0; i < num_threads; i++)
            arr_thread[i] = std::thread(thread_routine, &arr_thread_info[i]);

        for (long int i = 0; i < num_threads; i++)
            arr_thread[i].join();

        free(arr_thread_info);
        delete[] arr_thread;
    }
    catch (std::exception& error)
    {
        std::cout << error.what();
        free(arr_thread_info);
    }

    return E_SUCCESS;
}

static void fill_matrix_from_block_matrix(double* data, uint64_t rows, uint64_t cols, matrix_of_blocks* blocks)
{
    assert(data != nullptr);
    assert(blocks != nullptr);

    int block_dim = blocks->matrix_[0].cols_;
    uint64_t block_matr_cols = blocks->cols_;
    block* matr_blocks = blocks->matrix_;

    for (uint64_t row = 0; row < rows; row++)
        for (uint64_t col = 0; col < cols; col++)
        {
            uint64_t block_row = row / block_dim;
            uint64_t block_col = col / block_dim;
            int row_in_block   = row % block_dim;
            int col_in_block   = col % block_dim;
            data[row * cols + col] = matr_blocks[block_row * block_matr_cols + block_col].data_[row_in_block * block_dim + col_in_block];
        }
}

matrix matrix::block_mult(matrix& B, long int num_threads)
{
    if (B.rows_ != columns_)
        throw std::invalid_argument("[matrix::block_mult] incompatible matrix format\n");

    matrix_of_blocks* A_block_matrix = produce_block_matrix(columns_, rows_, data_);
    if (A_block_matrix == nullptr)
        throw std::runtime_error("[matrix::block_mult] produce A block matrix return error\n");

    matrix_of_blocks* B_trans_block_matrix = produce_trans_block(B.columns_, B.rows_, B.data_);
    if (B_trans_block_matrix == nullptr)
    {
        block_distruct(A_block_matrix);
        throw std::runtime_error("[matrix::block_mult] produce B block matrix return error\n");
    }

    matrix_of_blocks* C_block_matrix = produce_block_matrix(B.columns_, rows_, nullptr);
    if (C_block_matrix == nullptr)
    {
        block_distruct(A_block_matrix);
        block_distruct(B_trans_block_matrix);
        throw std::runtime_error("[matrix::block_mult] produce C block matrix return error\n");
    }

    int ret = mult_prep_block_matr_multitread(A_block_matrix, B_trans_block_matrix, C_block_matrix, num_threads);
    if (ret != E_SUCCESS)
    {
        block_distruct(A_block_matrix);
        block_distruct(B_trans_block_matrix);
        block_distruct(C_block_matrix);
        throw std::runtime_error("[matrix::block_mult] multithread mult of prepared matrix returned error\n");
    }
    debug_print_block_matrix(C_block_matrix, "C_block_matrix.matr");
    //debug_print_block_matrix(A_block_matrix, "A_block_matrix.matr");
    //debug_print_block_matrix(B_trans_block_matrix, "B_block_matrix.matr");

    block_distruct(A_block_matrix);
    block_distruct(B_trans_block_matrix);

    matrix C(B.columns_, rows_);

    fill_matrix_from_block_matrix(C.data_, C.rows_, C.columns_, C_block_matrix);

    return C;
}
