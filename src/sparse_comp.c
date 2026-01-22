#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "sparse_comp.h"

struct entry
{
    size_t minor;
    float value;
};

static int comp_build_ptr_from_coo(size_t *ptr, size_t major_dim, size_t minor_dim, size_t nval, size_t *major, size_t *minor, float  *values)
{
    for (size_t i = 0; i < nval; i++)
    {
        if (major[i] >= major_dim || minor[i] >= minor_dim)
            return -EINVAL;

        if (values[i] != 0.0f)
            ptr[major[i] + 1]++;
    }

    for (size_t i = 0; i < major_dim; i++)
        ptr[i + 1] += ptr[i];

    return 0;
}

static int comp_scatter_from_coo(struct comp_mat *mat, size_t nval, size_t *major, size_t *minor, float *values)
{
    size_t *next = malloc(mat->major_dim * sizeof(*next));
    if (next == NULL)
        return -ENOMEM;

    memcpy(next, mat->ptr, mat->major_dim * sizeof(*mat->ptr));

    for (size_t i = 0; i < nval; i++)
    {
        if (major[i] >= mat->major_dim || minor[i] >= mat->minor_dim)
        {
            free(next);
            return -EINVAL;
        }

        if (values[i] == 0.0f)
            continue;

        size_t pos = next[major[i]]++;
        mat->ind[pos] = minor[i];
        mat->values[pos] = values[i];
    }

    free(next);

    return 0;
}

static int cmp_entry(const void *a, const void *b)
{
    const size_t ca = ((const struct entry *) a)->minor;
    const size_t cb = ((const struct entry *) b)->minor;

    return (ca > cb) - (ca < cb);
}

static size_t comp_canonicalize_slice(size_t *slice, float *values, size_t len, struct entry *out)
{
    if (len == 0)
        return 0;

    for (size_t i = 0; i < len; i++)
    {
        out[i].minor = slice[i];
        out[i].value = values[i];
    }

    qsort(out, len, sizeof(*out), cmp_entry);

    size_t out_len = 1;
    for (size_t i = 1; i < len; i++)
    {
        if (out[i].minor == out[out_len - 1].minor)
            out[out_len - 1].value += out[i].value;
        else
            out[out_len++] = out[i];
    }

    return out_len;
}

static int comp_canonicalize(struct comp_mat *mat)
{
    size_t write_pos = 0;
    size_t max_len = 0;
    struct entry *entries = NULL;

    for (size_t major = 0; major < mat->major_dim; major++)
    {
        size_t major_start = mat->ptr[major];
        size_t major_end = mat->ptr[major + 1];
        size_t len = major_end - major_start;

        mat->ptr[major] = write_pos;

        if (len == 0)
            continue;
        
        if (len > max_len)
        {
            max_len = len;

            struct entry *tmp = realloc(entries, max_len * sizeof(*entries));
            if (tmp == NULL)
            {
                free(entries);
                return -ENOMEM;
            }

            entries = tmp;
        }

        size_t out_len = comp_canonicalize_slice(&mat->ind[major_start], &mat->values[major_start], len, entries);
        for (size_t i = 0; i < out_len; i++)
        {

            if (entries[i].value == 0.0f)
                continue;

            mat->ind[write_pos] = entries[i].minor;
            mat->values[write_pos]  = entries[i].value;
            write_pos++;
        }
    }

    if (entries != NULL)
        free(entries);

    mat->ptr[mat->major_dim] = write_pos;

    float *resized_values = realloc(mat->values, write_pos * sizeof(*mat->values));
    if (resized_values != NULL)
        mat->values = resized_values;

    size_t *resized_ind = realloc(mat->ind, write_pos * sizeof(*mat->ind));
    if (resized_ind != NULL)
        mat->ind = resized_ind;

    return 0;
}

int comp_from_coo(struct comp_mat *mat, size_t major_dim, size_t minor_dim, size_t nval, size_t *major, size_t *minor, float *values)
{
    if (mat == NULL || (nval > 0 && (major == NULL || minor == NULL || values == NULL)))
        return -EINVAL;

    memset(mat, 0, sizeof(*mat));

    mat->major_dim = major_dim;
    mat->minor_dim = minor_dim;

    mat->ptr = calloc((major_dim + 1), sizeof(*mat->ptr));
    if (mat->ptr == NULL)
        return -ENOMEM;

    int ret = comp_build_ptr_from_coo(mat->ptr, major_dim, minor_dim, nval, major, minor, values);
    if (ret != 0)
    {
        comp_free(mat);
        return ret;
    }

    size_t nnz = mat->ptr[major_dim];
    if (nnz == 0)
    {
        mat->ind = NULL;
        mat->values = NULL;
        return 0;
    }

    mat->values = malloc(nnz * sizeof(*mat->values));
    mat->ind = malloc(nnz * sizeof(*mat->ind));
    if (mat->values == NULL || mat->ind == NULL)
    {
        comp_free(mat);
        return -ENOMEM;
    }

    ret = comp_scatter_from_coo(mat, nval, major, minor, values);
    if (ret != 0)
    {
        comp_free(mat);
        return ret;
    }

    ret = comp_canonicalize(mat);
    if (ret != 0)
    {
        comp_free(mat);
        return ret;
    }

    return 0;
}

int comp_transpose(struct comp_mat *src, struct comp_mat *dst)
{
    if (src == NULL || dst == NULL)
        return -EINVAL;
    
    memset(dst, 0, sizeof(*dst));

    dst->major_dim = src->minor_dim;
    dst->minor_dim = src->major_dim;

    size_t nnz = src->ptr[src->major_dim];

    dst->ptr = calloc(dst->major_dim + 1, sizeof(*dst->ptr));
    dst->ind = malloc(nnz * sizeof(*dst->ind));
    dst->values = malloc(nnz * sizeof(*dst->values));
    if (dst->ptr == NULL || dst->ind == NULL || dst->values == NULL)
    {
        comp_free(dst);
        return -ENOMEM;
    }

    for (size_t i = 0; i < nnz; i++)
        dst->ptr[src->ind[i] + 1]++;

    for (size_t i = 0; i < dst->major_dim; i++)
        dst->ptr[i + 1] += dst->ptr[i];

    size_t *next = malloc(dst->major_dim * sizeof(*next));
    if (next == NULL)
    {
        comp_free(dst);
        return -ENOMEM;
    }
    memcpy(next, dst->ptr, dst->major_dim * sizeof(*next));

    for (size_t i = 0; i < src->major_dim; i++)
    {
        for (size_t k = src->ptr[i]; k < src->ptr[i + 1]; k++)
        {
            size_t j = src->ind[k];
            size_t pos = next[j]++;
            dst->ind[pos] = i;
            dst->values[pos] = src->values[k];
        }
    }

    free(next);

    return 0;
}

void comp_free(struct comp_mat *mat)
{
    if (mat == NULL)
        return;

    free(mat->ptr);
    free(mat->ind);
    free(mat->values);

    memset(mat, 0, sizeof(*mat));
}
