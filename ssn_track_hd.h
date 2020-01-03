#pragma once
/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2019 Adam Keeton
 * TCP session tracker, with timeouts. Uses a "blue-green" mechanism for 
 * timeouts and automatic hash resizing. Resizing and timeouts are handled in 
 * their own thread
*/

#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdbool.h>
#include <time.h>

#define SSNT_DEFAULT_NUM_ROWS 101197 // 1000003 // <-- large prime
#define SSNT_DEFAULT_TIMEOUT 60 // seconds
#define SSNT_DEFAULT_REFRESH_PERIOD 60 // seconds

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

typedef struct _ssnt_row_t {
    void *data;
    ssnt_key_t key;
} ssnt_row_t;

typedef struct _ssnt_config_t {
    uint32_t init_num_rows,
             refresh_period,
             timeout;
    int log_level;
} ssnt_config_t;

typedef struct _ssnt_tbl_t {
    // The callback to clean up user data
    void (*free_cb)(void *);

    // Running stats for this table
    // "collisions" considered when resizing the next hash
    uint64_t inserted, 
             collisions;

    uint64_t num_rows;
    ssnt_row_t **rows;
} ssnt_tbl_t;

typedef struct _ssnt_t {
    uint64_t refresh_period,
             timeout;

    bool running,
         refreshing;
    pthread_mutex_t lock;
    pthread_t refresh;

    // Our active table
    ssnt_tbl_t *active;

    // Our on-deck table
    ssnt_tbl_t *standby;
} ssnt_t;

#ifdef __cplusplus
extern "C" {
#endif

ssnt_t *ssnt_new_defaults(void (*free_cb)(void *));
ssnt_t *ssnt_new(uint64_t rows, uint32_t timeout_seconds, void (*free_cb)(void *));
void ssnt_free(ssnt_t *);
void *ssnt_lookup(ssnt_t *tracker, ssnt_key_t *key);
ssnt_stat_t ssnt_insert(ssnt_t *tracker, ssnt_key_t *key, void *data);
void ssnt_delete(ssnt_t *tracker, ssnt_key_t *key);

extern ssnt_config_t ssnt_config;
#ifdef __cplusplus
}
#endif
