#ifndef SPARSE_COMP_H
#define SPARSE_COMP_H

#include <stddef.h>

struct comp_mat
{
    size_t major_dim;
    size_t minor_dim;

    size_t *ptr;
    size_t *ind;
    float  *values;
};

int comp_from_coo(struct comp_mat *mat, size_t major_dim, size_t minor_dim, size_t nval, size_t *major, size_t *minor, float *values);
int comp_transpose(struct comp_mat *src, struct comp_mat *dst);
void comp_free(struct comp_mat *mat);

#endif