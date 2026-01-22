#ifndef CSR_H
#define CSR_H

#include <stddef.h>

struct csr_mat
{
    size_t nrows;
    size_t ncols;

    size_t *row_ptr;
    size_t *col_ind;
    float  *values;
};

int  csr_from_coo(struct csr_mat *mat, size_t nrows, size_t ncols, size_t nnz, size_t *row, size_t *col, float  *values);
void csr_free(struct csr_mat *mat);

#endif