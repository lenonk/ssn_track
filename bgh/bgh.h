#pragma once
/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2020 Adam Keeton
 * TCP session tracker, with timeouts. Uses a "blue-green" mechanism for 
 * timeouts and automatic hash resizing. Resizing and timeouts are handled in 
 * their own thread
*/

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>
#include <time.h>

#define BGH_DEFAULT_TIMEOUT 60 // seconds
#define BGH_DEFAULT_REFRESH_PERIOD 120 // seconds
// When num_rows * hash_full_pct < number inserted, hash is considered 
// full and we won't insert.
#define BGH_DEFAULT_HASH_FULL_PCT 6.0 // 6 percent

typedef enum _bgh_stat_t {
    BGH_OK,
    BGH_FULL,
    BGH_ALLOC_FAILED,
    BGH_MEM_EXCEPTION,
    BGH_EXCEPTION
} bgh_stat_t;

typedef struct _bgh_config_t {
    uint64_t starting_rows,
             min_rows,
             max_rows;
    uint32_t timeout, // Seconds
             refresh_period; // Seconds
    float hash_full_pct,
          scale_up_pct,
          scale_down_pct;
} bgh_config_t;

typedef struct _bgh_key_t {
    // Ports are intentionally kept as uint32_ts as an optimization in the key
    // comparison function
    uint32_t sip;
    uint32_t sport;

    uint32_t dip;
    uint32_t dport;

    uint8_t vlan;
} bgh_key_t;

typedef struct _bgh_row_t {
    void *data;
    // Necessary to prevent drained or deleted rows from preventing lookups 
    // from working when there had been a collision
    bool deleted; 
    bgh_key_t key;
} bgh_row_t;

typedef struct _bgh_stats_t {
    uint64_t inserted, 
             collisions,
             max_inserts,
             num_rows;
    bool in_refresh;
} bgh_stats_t;

typedef struct _bgh_tbl_t {
    // The callback to clean up user data
    void (*free_cb)(void *);

    // Running stats for this table
    // "collisions" considered when resizing the next hash
    uint64_t inserted, 
             collisions,
             max_inserts;
    uint64_t num_rows;
    bgh_row_t **rows;
} bgh_tbl_t;

typedef struct _bgh_t {
    bgh_config_t config;

    bool running,
         refreshing;
    pthread_mutex_t lock;
    pthread_t refresh;

    // Our active table
    bgh_tbl_t *active;

    // Our standby table, used when refreshing
    bgh_tbl_t *standby;
} bgh_t;

#ifdef __cplusplus
extern "C" {
#endif

// Allocate new session tracker using default config
bgh_t *bgh_new(void (*free_cb)(void *));

// Allocate new session tracker using user config
bgh_t *bgh_config_new(bgh_config_t *config, void (*free_cb)(void *));

// Initialize a configuration with the default values
void bgh_config_init(bgh_config_t *config);

// Free session tracker
void bgh_free(bgh_t *tracker);

// Lookup entry
void *bgh_lookup(bgh_t *tracker, bgh_key_t *key);

// Insert entry
bgh_stat_t bgh_insert(bgh_t *tracker, bgh_key_t *key, void *data);

// Delete entry 
void bgh_clear(bgh_t *tracker, bgh_key_t *key);

// Populate given stats structure
void bgh_get_stats(bgh_t *tracker, bgh_stats_t *stats);

#ifdef __cplusplus
}
#endif
