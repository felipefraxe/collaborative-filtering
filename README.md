# User-Based Collaborative Filtering in C

## Overview

This project implements a **user-based collaborative filtering recommender system** entirely in **C**, with a strong focus on systems-level concerns such as:

- Sparse linear algebra
- Explicit memory management
- Cache-efficient data structures
- Parallel computation using a custom thread pool

The implementation follows a classical **k-nearest neighbors (k-NN)** approach over users, using **centered cosine similarity** or **[Pearson correlation](https://en.wikipedia.org/wiki/Pearson_correlation_coefficient)**.

The code is intended for **offline computation** of user neighborhoods and recommendation scores on **large, sparse datasets**, prioritizing predictability, performance, and control over abstractions.

---

## High-Level Pipeline

The recommender pipeline is composed of the following stages:

1. Read sparse ratings from a binary **COO (Coordinate)** file  
2. Convert **COO → CSR**
3. Mean-center each user’s ratings
4. Compute L2 norms of user vectors
5. Find top-*K* nearest neighbors per user (parallelized)
6. Aggregate weighted recommendations from neighbors

Each stage is implemented explicitly, without relying on external numerical, BLAS, or machine-learning libraries.

---

## Algorithmic Details

### Similarity Metric

User similarity is computed using **cosine similarity** over mean-centered vectors:
sim(u, v) = (u · v) / (||u|| * ||v||)

Only overlapping items contribute to the dot product, exploiting sparsity in the rating matrix.

---

### Neighborhood Construction

For each user:

- Similarities are computed against all other users
- The top-*K* most similar users are selected
- Selection is performed using fixed-size data structures to avoid full sorting

This step is parallelized across users using a thread pool.

---

### Recommendation Aggregation

Candidate items are gathered from the neighbors’ rating histories.

For each candidate item *i*, the predicted score is computed as:

score(i) = Σ(sim(u, v) · r(v, i)) / Σ(sim(u, v))

where the summation runs over neighbors *v* who rated item *i*.

Intermediate aggregation is performed using hash tables to avoid dense structures.

---

## Data Structures

- **CSR Matrix**
  - Efficient row-wise traversal
  - Compact storage for sparse data

- **Hash Tables**
  - Used for aggregating item scores per user
  - Avoids allocating dense vectors for recommendations

- **Thread Pool**
  - Fixed-size worker pool
  - Used to parallelize neighborhood computation

All data structures are manually implemented with explicit ownership and lifetimes.

---

## Design Goals

- No external dependencies beyond the C standard library and POSIX threads
- Predictable memory usage
- Cache-friendly traversal patterns
- Clear separation between data loading, computation, and aggregation
- Suitability for large, sparse datasets processed offline

---

## Scope

This project focuses exclusively on the **core recommendation engine**.  
It does **not** include:

- Online serving or inference APIs
- Model persistence beyond binary outputs
- Hyperparameter tuning or evaluation metrics
- Python bindings or runtime integration

---

## Build & Usage

Details for building and running the project are intentionally kept minimal and platform-agnostic.  
Refer to the source files for compilation instructions and expected input formats.

---

## License

This project is provided for educational and experimental purposes.
