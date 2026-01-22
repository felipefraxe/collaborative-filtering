#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sparse_comp.h"
#include "csr.h"

int csr_from_coo(struct csr_mat *mat, size_t nrows, size_t ncols, size_t nnz, size_t *row, size_t *col, float  *values)
{
    if (mat == NULL)
        return -EINVAL;

    struct comp_mat tmp;
    int ret = comp_from_coo(&tmp, nrows, ncols, nnz, row, col, values);
    if (ret != 0)
        return ret;

    mat->nrows = nrows;
    mat->ncols = ncols;
    mat->row_ptr = tmp.ptr;
    mat->col_ind = tmp.ind;
    mat->values = tmp.values;

    return 0;
}

void csr_free(struct csr_mat *mat)
{
    if (mat == NULL)
        return;

    free(mat->row_ptr);
    free(mat->col_ind);
    free(mat->values);

    memset(mat, 0, sizeof(*mat));
}