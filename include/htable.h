#ifndef HTABLE_H
#define HTABLE_H

#include <stddef.h>
#include <stdbool.h>

struct value
{
    float weighted_sum;
    float weight_sum;
};

struct node
{
    size_t key;
    struct value value;
    struct node *next;
};

struct htable
{
    size_t capacity;
    size_t size;
    struct node **buckets;
};

void htable_init(struct htable *htable);
void htable_insert(struct htable *htable, size_t key, struct value value);
struct node *htable_find(struct htable *htable, size_t key);
void htable_free(struct htable *htable);

#endif