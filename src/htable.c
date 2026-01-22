#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "htable.h"

#if SIZE_MAX == UINT64_MAX
    #define HASH_A 11400714819323198485ull  // Knuth golden ratio
    #define HASH_W 64
#else
    #define HASH_A 2654435761u
    #define HASH_W 32
#endif

void htable_init(struct htable *htable)
{
    memset(htable, 0, sizeof(*htable));

    htable->capacity = 16;
    htable->size = 0;
    htable->buckets = calloc(htable->capacity, sizeof(*htable->buckets));
    if (htable->buckets == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }
}

static inline size_t hash(size_t key, size_t capacity)
{
    size_t r = __builtin_ctzl(capacity);
    return (key * HASH_A) >> (HASH_W - r);
}

struct node *htable_find(struct htable *htable, size_t key)
{
    size_t i = hash(key, htable->capacity);
    for (struct node *curr = htable->buckets[i]; curr != NULL; curr = curr->next)
    {
        if (curr->key == key)
            return curr;
    }

    return NULL;
}

static inline struct node *node_alloc(size_t key, struct value value)
{
    struct node *node = malloc(sizeof(*node));
    if (node == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }
    node->key = key;
    node->value = value;
    node->next = NULL;
    return node;
}

static void htable_resize(struct htable *htable)
{
    size_t new_capacity = (htable->capacity << 1);

    struct node **new_nodes = calloc(new_capacity, sizeof(*new_nodes));
    if (new_nodes == NULL)
    {
        fprintf(stderr, "Memory allocation error\n");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < htable->capacity; i++)
    {
        struct node *curr = htable->buckets[i];
        while (curr != NULL)
        {
            struct node *next = curr->next;

            size_t j = hash(curr->key, new_capacity);
            curr->next = new_nodes[j];
            new_nodes[j] = curr;

            curr = next;
        }
    }

    free(htable->buckets);
    htable->buckets = new_nodes;
    htable->capacity = new_capacity;
}

void htable_insert(struct htable *htable, size_t key, struct value value)
{
    size_t i = hash(key, htable->capacity);

    for (struct node *curr = htable->buckets[i]; curr != NULL; curr = curr->next)
    {
        if (curr->key == key)
        {
            curr->value = value;
            return;
        }
    }

    struct node *node = node_alloc(key, value);

    node->next = htable->buckets[i];
    htable->buckets[i] = node;
    htable->size++;

    if (htable->size * 10 >= htable->capacity * 8)
        htable_resize(htable);
}

void htable_free(struct htable *htable)
{
    if (htable == NULL)
        return;

    for (size_t i = 0; i < htable->capacity; i++)
    {
        while (htable->buckets[i] != NULL)
        {
            struct node *tmp = htable->buckets[i];
            htable->buckets[i] = htable->buckets[i]->next;
            free(tmp);
        }        
    }

    free(htable->buckets);

    memset(htable, 0, sizeof(*htable));
}