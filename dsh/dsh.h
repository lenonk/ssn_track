#pragma once

/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2019 Adam Keeton
 * TCP session tracker, with timeouts. Uses simple hash
*/

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define DSH_DEFAULT_NUM_ROWS 1000003 // <-- large prime
#define DSH_DEFAULT_TIMEOUT 60 // seconds

typedef enum _dsh_stat_t {
    DSH_OK,
    DSH_FULL,
    DSH_ALLOC_FAILED,
    DSH_MEM_EXCEPTION,
    DSH_EXCEPTION
} dsh_stat_t;

enum dsh_log_level_t {
    DSH_DEBUG,
    DSH_INFO,
    DSH_ERROR
};

typedef struct _dsh_key_t {
    uint32_t sip, dip;
    uint16_t sport, dport;
    uint8_t vlan;
} dsh_key_t;

typedef struct _dsh_lru_node_t {
    struct _dsh_lru_node_t *prev, *next;
    uint64_t idx;
    uint32_t last;
} dsh_lru_node_t;

typedef struct _dsh_lru_t {
    dsh_lru_node_t *head, *tail;
} dsh_lru_t;

typedef struct _dsh_row_t {
    void *data;
    dsh_key_t key;
    dsh_lru_node_t *to_node;
    bool deleted;
} dsh_row_t;

typedef struct _dsh_config_t {
    uint32_t num_rows,
             timeout;
    int log_level;
} dsh_config_t;

typedef struct _dsh_stats_t {
    uint64_t inserted,
             collisions,
             timeouts;
} dsh_stats_t;

typedef struct _dsh_t {
    uint64_t num_rows,
             timeout;

    dsh_stats_t stats;

    // Our top level table
    dsh_row_t **rows;
    void (*free_cb)(void *);

    // LRU for timing out stale entries
    dsh_lru_t *timeouts;
} dsh_t;

#ifdef __cplusplus
extern "C" {
#endif

dsh_t *dsh_new_defaults(void (*free_cb)(void *));
dsh_t *dsh_new(uint32_t rows, uint32_t timeout_seconds, void (*free_cb)(void *));
// Needs test case
// dsh_t *dsh_resize(dsh_t *existing, uint32_t new_size);
void dsh_free(dsh_t *);
void *dsh_lookup(dsh_t *tracker, dsh_key_t *key);
dsh_stat_t dsh_insert(dsh_t *tracker, dsh_key_t *key, void *data);
void dsh_delete(dsh_t *tracker, dsh_key_t *key);
void dsh_timeout_old(dsh_t *table, int max_age); // Call to timeout old nodes manually

extern dsh_config_t dsh_config;
#ifdef __cplusplus
}
#endif
