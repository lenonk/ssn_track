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

#define SSNT_DEFAULT_NUM_ROWS 1000003 // <-- large prime
#define SSNT_DEFAULT_TIMEOUT 60 // seconds

typedef enum _ssnt_stat_t {
    SSNT_OK,
    SSNT_FULL,
    SSNT_ALLOC_FAILED,
    SSNT_MEM_EXCEPTION,
    SSNT_EXCEPTION
} ssnt_stat_t;

enum ssnt_log_level_t {
    SSNT_DEBUG,
    SSNT_INFO,
    SSNT_ERROR
};

typedef struct _ssnt_key_t {
    uint32_t sip, dip;
    uint16_t sport, dport;
    uint8_t vlan;
} ssnt_key_t;

typedef struct _ssnt_lru_node_t {
    struct _ssnt_lru_node_t *prev, *next;
    uint64_t idx;
    uint32_t last;
} ssnt_lru_node_t;

typedef struct _ssnt_lru_t {
    ssnt_lru_node_t *head, *tail;
} ssnt_lru_t;

typedef struct _ssnt_row_t {
    void *data;
    ssnt_key_t key;
    ssnt_lru_node_t *to_node;
} ssnt_row_t;

typedef struct _ssnt_config_t {
    uint32_t num_rows,
             timeout;
    int log_level;
} ssnt_config_t;

typedef struct _ssnt_stats_t {
    uint64_t inserted,
             collisions,
             timeouts;
} ssnt_stats_t;

typedef struct _ssnt_t {
    uint64_t num_rows,
             timeout;

    ssnt_stats_t stats;

    // Our top level table
    ssnt_row_t **rows;
    void (*free_cb)(void *);

    // LRU for timing out stale entries
    ssnt_lru_t *timeouts;
} ssnt_t;

#ifdef __cplusplus
extern "C" {
#endif

ssnt_t *ssnt_new_defaults(void (*free_cb)(void *));
ssnt_t *ssnt_new(uint32_t rows, uint32_t timeout_seconds, void (*free_cb)(void *));
// Needs test case
// ssnt_t *ssnt_resize(ssnt_t *existing, uint32_t new_size);
void ssnt_free(ssnt_t *);
void *ssnt_lookup(ssnt_t *tracker, ssnt_key_t *key);
ssnt_stat_t ssnt_insert(ssnt_t *tracker, ssnt_key_t *key, void *data);
void ssnt_delete(ssnt_t *tracker, ssnt_key_t *key);
void ssnt_timeout_old(ssnt_t *table, int max_age); // Call to timeout old nodes manually

extern ssnt_config_t ssnt_config;
#ifdef __cplusplus
}
#endif
