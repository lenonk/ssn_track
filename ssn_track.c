/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2019 Adam Keeton
 * TCP session tracker, with timeouts. Uses simple hash
*/

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <openssl/md5.h>
#include <pthread.h>
#include "ssn_track.h"

ssnt_config_t ssnt_config = { SSNT_DEFAULT_NUM_ROWS, SSNT_DEFAULT_TIMEOUT, SSNT_INFO };

void debug(const char *args, ...) {
    if(ssnt_config.log_level > SSNT_DEBUG)
        return;

    va_list ap;
    va_start(ap, args);
    printf("DEBUG: ");
    vprintf(args, ap);
    puts("");
    va_end(ap);
}

void ssnt_debug_struct(ssnt_t *table) {
    if(ssnt_config.log_level > SSNT_DEBUG)
       return;

    debug("Table %p stats:", table);
    debug("    rows: %d", table->num_rows);
    debug("    currently inserted: %llu", table->stats.inserted);
    debug("    collisions: %llu", table->stats.collisions);
    debug("    timeouts: %llu", table->stats.timeouts);

    debug("  Nodes:");
    uint32_t counted_nodes = 0;
    ssnt_lru_node_t *node = table->timeouts->head;
    time_t now = time(NULL);

    for(; node; node = node->next) {
        debug("    Timeout node: %p, idx %d, last: %u", node, node->idx, node->last);
        if((now - node->last) > ssnt_config.timeout) {
            debug("     WARNING: Node should be timed out");
        }
        ssnt_row_t *row = table->rows[node->idx];
        assert(row->data);
        debug("        key: %u:%u %u:%u %u", 
            row->key.sip, row->key.dip, row->key.sport, row->key.dport, row->key.vlan);
        counted_nodes++;
        if(!node->next && node != table->timeouts->tail) {
            debug("WARNING: tail appears to be dangling");
            abort();
        }    
    }

    if(counted_nodes != table->stats.inserted) {
        debug("WARNING: Counted nodes != expected nodes: %d != %d", 
            counted_nodes, table->stats.inserted);
        abort();
    }
    else
        debug("Counted nodes: %d", counted_nodes);
}

void ssnt_lru_node_init(ssnt_lru_node_t *node) {
    // Revise so this isn't necessary?
    node->prev = node->next = NULL;
    // Set to 0xff..ff, ie, the distant future
    node->last = (uint32_t)-1;
}

ssnt_lru_node_t *ssnt_lru_new_node() {
    ssnt_lru_node_t *node = (ssnt_lru_node_t*)calloc(sizeof(ssnt_lru_node_t), 1);
    if(!node) 
        return NULL;
    ssnt_lru_node_init(node);
    return node;
}

ssnt_row_t *ssnt_new_row() {
    ssnt_row_t *row = (ssnt_row_t*)calloc(sizeof(ssnt_row_t), 1);
    if(!row)
        return NULL;
    row->to_node = ssnt_lru_new_node(0);
    return row;
};

ssnt_t *ssnt_new_defaults(void (*free_cb)(void *)) {
    return ssnt_new(SSNT_DEFAULT_NUM_ROWS, SSNT_DEFAULT_TIMEOUT, free_cb);
}

ssnt_t *ssnt_new(uint32_t rows, uint32_t timeout_seconds, void (*free_cb)(void *)) {
    ssnt_t *table = (ssnt_t*)malloc(sizeof(ssnt_t));
    if(!table)
        return NULL;

    table->num_rows = rows;
    table->timeout = timeout_seconds;
    table->rows = (ssnt_row_t**)malloc(sizeof(ssnt_row_t)*table->num_rows);
    if(!table->rows) {
        free(table);
        return NULL;
    }

    for(int i=0; i<table->num_rows; i++)
        table->rows[i] = ssnt_new_row();

    table->timeouts = (ssnt_lru_t*)calloc(sizeof(ssnt_lru_t), 1);
    if(!table->timeouts) {
        free(table->rows);
        free(table);
        return NULL;
    }

    table->free_cb = free_cb;
    memset(&table->stats, 0, sizeof(table->stats));
    return table;
}

// XXX Needs test case
#if 0
ssnt_t *ssnt_resize(ssnt_t *orig, uint32_t new_size) {
    if(new_size == orig->num_rows)
        return orig;

    ssnt_t *table = ssnt_new(new_size, orig->timeout, orig->free_cb);
    if(!table)
        return NULL;

    // Going to use the timeouts in the orig table to find all the existing
    // nodes
    free(table->timeouts);
    table->timeouts = orig->timeouts;

    for(ssnt_lru_node_t *node = orig->timeouts->head; node; node = node->next) {
        ssnt_row_t *old_row = orig->rows[node->idx];
        uint64_t idx = _lookup(table, &old_row->key);
        // Save new index in timeout node
        node->idx = idx;
        // Copy over row data
        ssnt_row_t *new_row = table->rows[idx];
        memcpy(&new_row->key, &old_row->key, sizeof(old_row->key));
        new_row->to_node = node;
        new_row->data = old_row->data;
        table->stats.inserted++;
    }

    ssnt_free(orig);   
}
#endif

void ssnt_free(ssnt_t *table) {
    if(!table) return;

    for(ssnt_lru_node_t *node = table->timeouts->head; node; node = node->next) {
        // debug("%s: Deleting TO node %p idx %d", __func__, node, node->idx);
        table->free_cb(table->rows[node->idx]->data);
    }

    free(table->timeouts);

    for(int i=0; i<table->num_rows; i++) {
        free(table->rows[i]->to_node);
        free(table->rows[i]);
    }

    free(table->rows);
    free(table);
}

void ssnt_timeout_old(ssnt_t *table, int max_age) {
    // Iterate backwards from tail, removing old nodes
    time_t now = time(NULL);

    // puts("Timeout thingy starting");
    // ssnt_debug_struct(table);

    ssnt_lru_t *timeouts = table->timeouts;
    ssnt_lru_node_t *oldest = timeouts->tail;

    for(ssnt_lru_node_t *current = timeouts->tail; current; ) {
        if((now - current->last) < max_age) {
            timeouts->tail = current;
            current->next = NULL;
            break;
        }

        debug("Timing out node %p for idx %d: %d >= %d", 
            current, current->idx, now - current->last, max_age);

        ssnt_row_t *row = table->rows[current->idx];

        if(ssnt_config.log_level <= SSNT_DEBUG)
            assert(row->data);

        table->stats.inserted--;
        table->stats.timeouts++;
        table->free_cb(row->data);
        row->data = NULL;

        current = current->prev;

        // NOTE: this changes the pointers in the node hence changing current above
        ssnt_lru_node_init(row->to_node);
    
        if(!current) 
            // Walked entire list
            timeouts->tail = timeouts->head = NULL;
    }

    ssnt_debug_struct(table);
}

void ssnt_timeout_update(ssnt_t *table, ssnt_row_t *row, uint64_t idx) {
    ssnt_lru_t *timeouts = table->timeouts;
    ssnt_lru_node_t *node;

    node = row->to_node;

    if(node != timeouts->head) {
        if(node->prev) {
            node->prev->next = node->next;

            if(timeouts->tail == node)
                timeouts->tail = node->prev;
        }

        if(node->next)
            node->next->prev = node->prev;

        node->next = timeouts->head;
        node->prev = NULL;
        if(timeouts->head)
            timeouts->head->prev = node;
        timeouts->head = node;
    }

    node->last = time(NULL);

    if(!node->next)
        timeouts->tail = node;
}

void _ssnt_timeout_remove(ssnt_lru_t *timeouts, ssnt_lru_node_t *node) {
    if(timeouts->head == node) {
        timeouts->head = node->next;
        if(timeouts->head)
            timeouts->head->prev = NULL;
        if(timeouts->tail == node)
            timeouts->tail = timeouts->head;
    }
    else {
        if(node->prev)
            node->prev->next = node->next;
        if(node->next)
            node->next->prev = node->prev;
        if(timeouts->tail == node)
            timeouts->tail = node->prev;
    }

    // debug("%s: Reinit'ing TO node %p idx %d", __func__, node, node->idx);
    ssnt_lru_node_init(node);
}

static int key_eq(ssnt_key_t *k1, ssnt_key_t *k2) {
    return ((k1->sip == k2->sip &&
            k1->sport == k2->sport &&
            k1->dip == k2->dip && 
            k1->dport == k2->dport)
                ||
           (k1->sip == k2->dip && 
            k1->sport == k2->dport &&
            k1->dip == k2->sip && 
            k1->dport == k2->sport))
                && 
           k1->vlan == k2->vlan;
}

// Hash func: XOR32
// Reference: https://www.researchgate.net/publication/281571413_COMPARISON_OF_HASH_STRATEGIES_FOR_FLOW-BASED_LOAD_BALANCING
static inline uint64_t hash_func(uint64_t mask, ssnt_key_t *key) {
#if 1
    uint64_t h = ((uint64_t)(key->sip + key->dip) ^
                            (key->sport + key->dport));
    h *= 1 + key->vlan;
#else
    // XXX Gave similar distribution performance to the above
    MD5_CTX c;
    MD5_Init(&c);
    MD5_Update(&c, key, sizeof(*key));
    unsigned char digest[16];
    MD5_Final(digest, &c);
    
    uint64_t h = *(uint64_t*)digest;
    debug("HASH: %llu -> %ld", h, h % mask); 
#endif
    return h % mask;
}

static int64_t _lookup(ssnt_t *table, ssnt_key_t *key) {
    int64_t idx = hash_func(table->num_rows, key);
    ssnt_row_t *row = table->rows[idx];

    // If nothing is stored here, just return it anyway.
    // We'll check later
    if(!row->data || key_eq(key, &row->key))
        return idx;

    // There was a collision. Use linear probing

    uint64_t start = idx++;
    while(idx != start) {
        // debug("Index %d collides", idx);

        table->stats.collisions++;

        if(idx >= table->num_rows)
            idx = 0;

        ssnt_row_t *row = table->rows[idx];

        if(!row->data || key_eq(key, &row->key))
            return idx;

        idx++;
    }

    return -1;
}

ssnt_stat_t ssnt_insert(ssnt_t *table, ssnt_key_t *key, void *data) {
    // null data is not allowed
    // data is used to check if a row is used
    if(!data)
        return SSNT_EXCEPTION;

    ssnt_timeout_old(table, table->timeout);

    if(table->stats.inserted * 8 > table->num_rows)
        return SSNT_FULL;

    int64_t idx = _lookup(table, key);
    if(idx < 0)
        return SSNT_EXCEPTION;

    ssnt_row_t *row = table->rows[idx];

    if(row->data) {
        table->free_cb(row);
    }

    memcpy(&row->key, key, sizeof(row->key));
    table->stats.inserted++;
    row->data = data;
    row->to_node->idx = idx;

    ssnt_timeout_update(table, row, idx);

    return SSNT_OK;
}

void *ssnt_lookup(ssnt_t *table, ssnt_key_t *key) {
    int64_t idx = _lookup(table, key);

    if(idx < 0)
        return NULL;

    ssnt_timeout_old(table, table->timeout);

    ssnt_row_t *row = table->rows[idx];

    if(!row->data)
        return NULL;

    ssnt_timeout_update(table, row, idx);

    return row->data;
}

void ssnt_delete(ssnt_t *table, ssnt_key_t *key) {
    int64_t idx = _lookup(table, key);
    if(idx < 0)
        return; // Should never happen

    ssnt_row_t *row = table->rows[idx];

    if(!row->data) 
        return;

    _ssnt_timeout_remove(table->timeouts, row->to_node);

    table->free_cb(row->data);
    table->stats.inserted--;
    row->data = NULL;
}

