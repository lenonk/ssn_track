#pragma once

/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 *
 * TCP session tracker, with timeouts. Uses simple hash
*/

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define SSNT_DEFAULT_NUM_ROWS 1024*1024
#define SSNT_DEFAULT_TIMEOUT 60 // seconds

enum ssnt_stat_t {
    SSNT_OK,
    SSNT_FULL,
    SSNT_ALLOC_FAILED,
    SSNT_EXCEPTION
};

enum ssnt_log_level_t {
    SSNT_DEBUG,
    SSNT_ERROR
};

struct ssnt_key_t {
    uint32_t sip, dip;
    uint16_t sport, dport;
    uint8_t vlan;
};

struct ssnt_lru_node_t {
    ssnt_lru_node_t *prev,
                    *next;
    uint64_t idx;
    time_t last;
};

struct ssnt_lru_t {
    ssnt_lru_node_t *head, 
                    *tail;
};

struct ssnt_row_t {
    void *data;
    ssnt_key_t key;
    ssnt_lru_node_t *to_node;
};

struct ssnt_t {
    uint64_t num_rows,
             timeout,
             inserted,
             // Stat tracking
             collisions;

    // Our top level table
    ssnt_row_t **rows;
    void (*free_cb)(void *);

    // LRU for timing out stale entries
    ssnt_lru_t *timeouts;
};

ssnt_t *ssnt_new(void (*free_cb)(void *));
ssnt_t *ssnt_new(uint32_t rows, uint32_t timeout_seconds, void (*free_cb)(void *));
void ssnt_free(ssnt_t *);
void *ssnt_lookup(ssnt_t *tracker, ssnt_key_t *key);
ssnt_stat_t ssnt_insert(ssnt_t *tracker, ssnt_key_t *key, void *data);
void ssnt_delete(ssnt_t *tracker, ssnt_key_t *key);

