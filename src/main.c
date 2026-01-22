#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "csr.h"
#include "htable.h"
#include "thread_pool.h"

#define TRIPLET_SIZE (2 * sizeof(uint64_t) + sizeof(uint8_t))

struct neighbor
{
    size_t id;
    float sim;
};

struct neighborhood
{
    size_t size;
    struct neighbor *neighbors;
};

struct job
{
    struct csr_mat *mat;
    struct neighborhood *neighborhood;
    float *norms;
    size_t k;
    size_t i;
};

struct coo
{
    size_t nrows;
    size_t ncols;
    size_t nvals;
    size_t *rows;
    size_t *cols;
    float *values;
};

void read_coo(const char *filepath, struct coo *mat);
void destroy_coo(struct coo *mat);
float mean(float *val, size_t len);
void mean_centering(struct csr_mat *src, struct csr_mat *dest);
float *get_norms(struct csr_mat *mat);
float cosine_sim(struct csr_mat *mat, float *norms, size_t row_a, size_t row_b);
void find_nearest_neighbors(void *arg);

static inline void swap(struct neighbor *arr, size_t i, size_t j);
static void sift_up(struct neighborhood *heap, size_t i);
static void sift_down(struct neighborhood *heap, size_t i);
void heap_build(struct neighborhood *heap);
struct neighbor heap_extract_min(struct neighborhood *heap);
void heap_insert(struct neighborhood *heap, struct neighbor neighbor);
void heap_insert_remove_min(struct neighborhood *heap, struct neighbor neighbor);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    struct coo coo_mat;
    read_coo(argv[1], &coo_mat);

    struct csr_mat mat;
    csr_from_coo(&mat, coo_mat.nrows, coo_mat.ncols, coo_mat.nvals, coo_mat.rows, coo_mat.cols, coo_mat.values);
    printf("Loaded Matrix: %zu x %zu with %zu non-zeros\n", mat.nrows, mat.ncols, mat.row_ptr[mat.nrows]);

    destroy_coo(&coo_mat);

    struct csr_mat centered_mat;
    mean_centering(&mat, &centered_mat);
    printf("Mean Centered Matrix\n");

    float *norms = get_norms(&centered_mat);
    printf("Computed row norms\n");

    const int K = 30;
    struct neighborhood *neighborhood = calloc(mat.nrows, sizeof(*neighborhood));
    if (neighborhood == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        return 1;
    }

    size_t num_threads = sysconf(_SC_NPROCESSORS_ONLN);
    tp_pool_t pool;
    if (tp_pool_init(&pool, num_threads) != TP_OK)
    {
        fprintf(stderr, "Thread pool initialization error\n");
        return 1;
    }

    for (size_t user = 0; user < mat.nrows; user++)
    {
        struct job *job = malloc(sizeof(*job));
        if (job == NULL)
        {
            fprintf(stderr, "Memory allocation error\n");
            return 1;
        }
        job->mat = &centered_mat;
        job->neighborhood = neighborhood;
        job->norms = norms;
        job->k = K;
        job->i = user;

        if (tp_pool_submit(&pool, find_nearest_neighbors, job) != TP_OK)
        {
            fprintf(stderr, "Thread pool submission error\n");
            tp_pool_destroy(&pool);
            return 1;
        }
    }
    tp_pool_destroy(&pool);

    free(norms);
    csr_free(&centered_mat);
    printf("Computed nearest neighbors\n");

    for (size_t user = 0; user < mat.nrows; user++)
    {
        struct htable items;
        htable_init(&items);

        size_t u_start = mat.row_ptr[user];
        size_t u_end = mat.row_ptr[user + 1];

        struct neighbor *neighbors = neighborhood[user].neighbors;
        for (size_t neighbor = 0; neighbor < neighborhood[user].size; neighbor++)
        {
            size_t v_start = mat.row_ptr[neighbors[neighbor].id];
            size_t v_end = mat.row_ptr[neighbors[neighbor].id + 1];

            size_t i = u_start;
            size_t j = v_start;
            while (i < u_end && j < v_end)
            {
                if (mat.col_ind[i] < mat.col_ind[j])
                    i++;
                else if (mat.col_ind[i] == mat.col_ind[j])
                {
                    i++;
                    j++;
                }
                else
                {
                    struct node *item = htable_find(&items, mat.col_ind[j]);
                    if (item == NULL)
                    {
                        struct value value = {
                            .weighted_sum = neighbors[neighbor].sim * mat.values[j],
                            .weight_sum = neighbors[neighbor].sim
                        };
                        htable_insert(&items, mat.col_ind[j], value);
                    }
                    else
                    {
                        item->value.weighted_sum += (neighbors[neighbor].sim * mat.values[j]);
                        item->value.weight_sum += neighbors[neighbor].sim;
                    }

                    j++;
                }
            }

            while (j < v_end)
            {
                struct node *item = htable_find(&items, mat.col_ind[j]);
                if (item == NULL)
                {
                    struct value value = {
                        .weighted_sum = neighbors[neighbor].sim * mat.values[j],
                        .weight_sum = neighbors[neighbor].sim
                    };
                    htable_insert(&items, mat.col_ind[j], value);
                }
                else
                {
                    item->value.weighted_sum += (neighbors[neighbor].sim * mat.values[j]);
                    item->value.weight_sum += neighbors[neighbor].sim;
                }

                j++;
            }
        }

        printf("Recommendations for User %zu:\n", user);
        for (size_t i = 0; i < items.capacity; i++)
        {
            for (struct node *curr = items.buckets[i]; curr != NULL; curr = curr->next)
            {
                float score = curr->value.weighted_sum / curr->value.weight_sum;
                printf("\tItem %zu: Score %.4f\n", curr->key, score);
            }
        }
        printf("\n");

        htable_free(&items);
    }

    free(neighborhood);
    csr_free(&mat);

    return 0;
}

void read_coo(const char *filepath, struct coo *mat)
{
    FILE *file = fopen(filepath, "rb");
    if (file == NULL)
    {
        fprintf(stderr, "Error opening file: %s\n", filepath);
        exit(1);
    }

    fread(&mat->nrows, sizeof(uint64_t), 1, file);
    fread(&mat->ncols, sizeof(uint64_t), 1, file);

    size_t nvals = 0;
    uint8_t buf[TRIPLET_SIZE];

    while (fread(buf, sizeof(buf), 1, file) == 1)
        nvals++;

    fseek(file, 2 * sizeof(uint64_t), SEEK_SET);

    mat->nvals = nvals;
    mat->rows = malloc(nvals * sizeof(size_t));
    mat->cols = malloc(nvals * sizeof(size_t));
    mat->values = malloc(nvals * sizeof(float));

    for (size_t i = 0; fread(buf, sizeof(buf), 1, file) == 1; i++)
    {
        size_t row, col;
        uint8_t val;
        memcpy(&row, &buf[0], sizeof(size_t));
        memcpy(&col, &buf[sizeof(uint64_t)], sizeof(size_t));
        memcpy(&val, &buf[2 * sizeof(uint64_t)], sizeof(uint8_t));

        mat->rows[i] = row;
        mat->cols[i] = col;
        mat->values[i] = (float) val;
    }

    fclose(file);
}

void destroy_coo(struct coo *mat)
{
    if (mat == NULL)
        return;

    free(mat->rows);
    free(mat->cols);
    free(mat->values);

    memset(mat, 0, sizeof(*mat));
}

float mean(float *val, size_t len)
{
    if (len == 0)
        return 0.0f;

    float sum = 0.0;
    for (size_t i = 0; i < len; i++)
        sum += val[i];
    return sum / (float) len;
}

void mean_centering(struct csr_mat *src, struct csr_mat *dest)
{
    dest->nrows = src->nrows;
    dest->ncols = src->ncols;

    dest->row_ptr = malloc((dest->nrows + 1) * sizeof(*dest->row_ptr));
    dest->col_ind = malloc(src->row_ptr[src->nrows] * sizeof(*dest->col_ind));
    dest->values = malloc(src->row_ptr[src->nrows] * sizeof(*dest->values));
    if (dest->row_ptr == NULL || dest->col_ind == NULL || dest->values == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    memcpy(dest->row_ptr, src->row_ptr, (dest->nrows + 1) * sizeof(*dest->row_ptr));
    memcpy(dest->col_ind, src->col_ind, src->row_ptr[src->nrows] * sizeof(*dest->col_ind));

    for (size_t user = 0; user < src->nrows; user++)
    {
        size_t row_start = src->row_ptr[user];
        size_t row_end = src->row_ptr[user + 1];

        float row_mean = mean(&src->values[row_start], row_end - row_start);
        for (size_t i = row_start; i < row_end; i++)
            dest->values[i] = src->values[i] - row_mean;
    }
}

float *get_norms(struct csr_mat *mat)
{
    float *norms = malloc(mat->nrows * sizeof(*norms));
    if (norms == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    for (size_t row = 0; row < mat->nrows; row++)
    {
        size_t row_start = mat->row_ptr[row];
        size_t row_end = mat->row_ptr[row + 1];

        float sum_sq = 0.0f;
        for (size_t i = row_start; i < row_end; i++)
            sum_sq += mat->values[i] * mat->values[i];

        norms[row] = (sum_sq > 0.0f) ? sqrtf(sum_sq) : 0.0f;
    }

    return norms;
}

float cosine_sim(struct csr_mat *mat, float *norm, size_t row_a, size_t row_b)
{
    if (norm[row_a] == 0.0f || norm[row_b] == 0.0f)
        return 0.0f;

    size_t i = mat->row_ptr[row_a];
    size_t a_end = mat->row_ptr[row_a + 1];

    size_t j = mat->row_ptr[row_b];
    size_t b_end = mat->row_ptr[row_b + 1];

    float dot_product = 0.0;
    while (i < a_end && j < b_end)
    {
        if (mat->col_ind[i] < mat->col_ind[j])
            i++;
        else if (mat->col_ind[i] > mat->col_ind[j])
            j++;
        else
            dot_product += mat->values[i++] * mat->values[j++];
    }

    return dot_product / (norm[row_a] * norm[row_b]);
}

void find_nearest_neighbors(void *arg)
{
    struct job *job = arg;

    struct csr_mat *mat = job->mat;
    struct neighborhood *neighborhood = job->neighborhood;
    float *norms = job->norms;
    size_t i = job->i;
    size_t k = job->k;

    struct neighborhood heap = {.size = 0, .neighbors = malloc(k * sizeof(*heap.neighbors))};
    if (heap.neighbors == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        return;
    }

    for (size_t j = 0; j < mat->nrows; j++)
    {
        float sim;
        if (i == j || (sim = cosine_sim(mat, norms, i, j)) <= 0.0f)
            continue;

        struct neighbor nb = {.id = j, .sim = sim};
        if (heap.size < k)
            heap_insert(&heap, nb);
        else if (heap.neighbors[0].sim < sim)
            heap_insert_remove_min(&heap, nb);
    }

    struct neighbor *neighbors = malloc(heap.size * sizeof(*neighbors));
    if (neighbors == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        return;
    }
    neighborhood[i].neighbors = neighbors;
    neighborhood[i].size = heap.size;

    while (heap.size > 0)
        neighbors[heap.size - 1] = heap_extract_min(&heap);

    free(heap.neighbors);
    free(job);
}

static inline void swap(struct neighbor *arr, size_t i, size_t j)
{
    struct neighbor tmp = arr[i];
    arr[i] = arr[j];
    arr[j] = tmp;
}

static void sift_up(struct neighborhood *heap, size_t i)
{
    while (i > 0)
    {
        size_t parent = (i - 1) / 2;
        if (heap->neighbors[i].sim >= heap->neighbors[parent].sim)
            return;
        swap(heap->neighbors, i, parent);
        i = parent;
    }
}

static void sift_down(struct neighborhood *heap, size_t i)
{
    for (;;)
    {
        size_t left = (2 * i) + 1;
        size_t right = left + 1;
        size_t smallest = i;

        if (left < heap->size && heap->neighbors[left].sim < heap->neighbors[smallest].sim)
            smallest = left;
        if (right < heap->size && heap->neighbors[right].sim < heap->neighbors[smallest].sim)
            smallest = right;

        if (smallest == i)
            return;
        swap(heap->neighbors, i, smallest);
        i = smallest;
    }
}

void heap_build(struct neighborhood *heap)
{
    if (heap->size <= 1)
        return;

    for (size_t i = (heap->size / 2); i-- > 0;)
        sift_down(heap, i);
}

struct neighbor heap_extract_min(struct neighborhood *heap)
{
    if (heap->size == 0)
        return (struct neighbor) {0};

    struct neighbor min = heap->neighbors[0];
    heap->size--;

    if (heap->size > 0)
    {
        heap->neighbors[0] = heap->neighbors[heap->size];
        sift_down(heap, 0);
    }

    return min;
}

void heap_insert(struct neighborhood *heap, struct neighbor neighbor)
{
    heap->neighbors[heap->size++] = neighbor;
    sift_up(heap, heap->size - 1);
}

void heap_insert_remove_min(struct neighborhood *heap, struct neighbor nb)
{
    if (heap->size == 0)
        return;
    heap->neighbors[0] = nb;
    sift_down(heap, 0);
}