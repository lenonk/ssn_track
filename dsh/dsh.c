/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2019 Adam Keeton
 * TCP session tracker, with timeouts. Uses simple hash
*/

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include "dsh.h"

dsh_config_t dsh_config = { DSH_DEFAULT_NUM_ROWS, DSH_DEFAULT_TIMEOUT, DSH_INFO };

void debug(const char *args, ...) {
    if(dsh_config.log_level > DSH_DEBUG)
        return;

    va_list ap;
    va_start(ap, args);
    printf("DEBUG: ");
    vprintf(args, ap);
    puts("");
    va_end(ap);
}

void dsh_debug_struct(dsh_t *table) {
    if(dsh_config.log_level > DSH_DEBUG)
       return;

    debug("Table %p stats:", table);
    debug("    rows: %d", table->num_rows);
    debug("    currently inserted: %llu", table->stats.inserted);
    debug("    collisions: %llu", table->stats.collisions);
    debug("    timeouts: %llu", table->stats.timeouts);

    debug("  Nodes:");
    uint32_t counted_nodes = 0;
    dsh_lru_node_t *node = table->timeouts->head;
    time_t now = time(NULL);

    for(; node; node = node->next) {
        debug("    Timeout node: %p, idx %d, last: %u", node, node->idx, node->last);
        if((now - node->last) > dsh_config.timeout) {
            debug("     WARNING: Node should be timed out");
        }
        dsh_row_t *row = table->rows[node->idx];
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

void dsh_lru_node_init(dsh_lru_node_t *node) {
    // Revise so this isn't necessary?
    node->prev = node->next = NULL;
    // Set to 0xff..ff, ie, the distant future
    node->last = (uint32_t)-1;
}

dsh_lru_node_t *dsh_lru_new_node() {
    dsh_lru_node_t *node = (dsh_lru_node_t*)calloc(sizeof(dsh_lru_node_t), 1);
    if(!node) 
        return NULL;
    dsh_lru_node_init(node);
    return node;
}

dsh_row_t *dsh_new_row() {
    dsh_row_t *row = (dsh_row_t*)calloc(sizeof(dsh_row_t), 1);
    if(!row)
        return NULL;
    row->to_node = dsh_lru_new_node();
    row->to_node->row = row;
    row->next = NULL;
    return row;
};

dsh_t *dsh_new_defaults(void (*free_cb)(void *)) {
    return dsh_new(DSH_DEFAULT_NUM_ROWS, DSH_DEFAULT_TIMEOUT, free_cb);
}

dsh_t *dsh_new(uint32_t rows, uint32_t timeout_seconds, void (*free_cb)(void *)) {
    dsh_t *table = (dsh_t*)malloc(sizeof(dsh_t));
    if(!table)
        return NULL;

    table->num_rows = rows;
    table->timeout = timeout_seconds;
    table->rows = (dsh_row_t**)malloc(sizeof(dsh_row_t)*table->num_rows);
    if(!table->rows) {
        free(table);
        return NULL;
    }

    for(int i=0; i<table->num_rows; i++)
        table->rows[i] = dsh_new_row();

    table->timeouts = (dsh_lru_t*)calloc(sizeof(dsh_lru_t), 1);
    if(!table->timeouts) {
        for(int i=0; i<table->num_rows; i++)
            free(table->rows[i]);
        free(table->rows);
        free(table);
        return NULL;
    }

    table->free_cb = free_cb;
    memset(&table->stats, 0, sizeof(table->stats));
    return table;
}

void dsh_free(dsh_t *table) {
    if(!table) return;

    for(dsh_lru_node_t *node = table->timeouts->head; node; node = node->next) {
        // debug("%s: Deleting TO node %p idx %d", __func__, node, node->idx);
        table->free_cb(table->rows[node->idx]->data);
if ->next, delete each
    }

    free(table->timeouts);

    for(int i=0; i<table->num_rows; i++) {
        free(table->rows[i]->to_node);
        free(table->rows[i]);
    }

    free(table->rows);
    free(table);
}

#if 0
void _clear_collision(dsh_row_t *top, dsh_row_t *row) {
    // Search the list for it
    dsh_row_t *cr = top;
              *crn = top->next;
    while(crn != row) {
        cr = cr->next;
        crn = crn->next;
    }
    assert(cr);
    assert(crn);
    cr->next = crn->next; // Unlink from collision list
    crn->free_cb(crn->data); // Free user data
    free(crn);
}
#endif

void _clear_collision(dsh_row_t *row, dsh_key_t *key) {
    // Search the list for it
    dsh_row_t *cr = top;
              *crn = top->next;
    while(!key_eq(crn->key, key)) {
        cr = cr->next;
        crn = crn->next;
    }
    assert(cr);
    assert(crn);
    cr->next = crn->next; // Unlink from collision list
    crn->free_cb(crn->data); // Free user data

    if(crn->to_node->prev)
        crn->to_node->prev->next = crn->to_node->next;
    free(crn);
}

void dsh_timeout_old(dsh_t *table, int max_age) {
    // Iterate backwards from tail, removing old nodes
    time_t now = time(NULL);

    // puts("Timeout thingy starting");
    // dsh_debug_struct(table);

    dsh_lru_t *timeouts = table->timeouts;

    for(dsh_lru_node_t *current = timeouts->tail; current; ) {
        if((now - current->last) < max_age) {
            timeouts->tail = current;
            current->next = NULL;
            break;
        }

        table->free_cb(current->row->data);

        debug("Timing out node %p for idx %d: %d >= %d", 
            current, current->idx, now - current->last, max_age);

        dsh_row_t *row = table->rows[current->idx];

        // In case of collision, row won't equal current->row
        // Need to specifically free that timeout node and this collided entry
        if(row != current->row) {
            _clear_collision(row, current->row->key);
            table->stats.collisions--;

            if(current->prev)
                current->prev->next = NULL;
            dsh_lru_node_t *tmp = current;
            current = current->prev;
            free(tmp);
        }
        else {
            row->data = NULL;
            current = current->prev;
            dsh_lru_node_init(row->to_node);
        }

        table->stats.inserted--;
        table->stats.timeouts++;
    
        if(!current) 
            // Walked entire list
            timeouts->tail = timeouts->head = NULL;
    }

    dsh_debug_struct(table);
}

// Insert this row's timeout node into the timeouts LRU
void dsh_timeout_update(dsh_t *table, dsh_row_t *row) {
    dsh_lru_t *timeouts = table->timeouts;
    dsh_lru_node_t *node = row->to_node;

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

void _dsh_timeout_remove(dsh_lru_t *timeouts, dsh_lru_node_t *node) {
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
    dsh_lru_node_init(node);
}

static int key_eq(dsh_key_t *k1, dsh_key_t *k2) {
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
static inline uint64_t hash_func(uint64_t mask, dsh_key_t *key) {
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

static dsh_row_t *_lookup_row(dsh_t *table, dsh_key_t *key) {
    int64_t idx = hash_func(table->num_rows, key);
    dsh_row_t *row = table->rows[idx];

    // If nothing is/was stored here, just return it anyway.
    // We'll check later
    // The check for "deleted" is to deal with the case where there was 
    // previously a collision
    if(!row->data || key_eq(key, &row->key))
        return row;

    for(row = row->next; row; row = row->next) {
        if(key_eq(key, &row->key)) {
            return row;
        }
    }

    return NULL;
}

// Returns the collided row, or allocates
// If it allocates a new one, it's inserted at the top of the list
dsh_row_t *_insert_collision(dsh_row_t *first, dsh_key_t *key) {
    for(row = first->next; row; row = row->next) {
        if(key_eq(key, &row->key)) {
            return row;
        }
    }

    // No row found. Allocate new and insert at front of collision list

    dsh_row_t *nr = dsh_new_row();
    dsh_row_t *tmp = first->next;
    first->next = nr;
    nr->next = tmp;

    return nr;
}

dsh_stat_t dsh_insert(dsh_t *table, dsh_key_t *key, void *data) {
    // null data is not allowed
    // data is used to check if a row is used
    if(!data)
        return DSH_EXCEPTION;

    dsh_timeout_old(table, table->timeout);

    if(table->stats.inserted * 8 > table->num_rows)
        return DSH_FULL;

    int64_t idx = hash_func(table->num_rows, key);
    dsh_row_t *row = table->rows[idx];

    if(row->data) {
        if(key_eq(key, &row->key)) {
            // Inserted over existing key. Free old data
            table->free_cb(row->data);
        else
            // Walk collision list, if present
            // allocate new node if necessary
            row = _insert_collision(row, key);
            table->stats.inserted++;
        } 
    }
    else {
        table->stats.inserted++;
    }

    row->to_node->idx = idx;
    memcpy(&row->key, key, sizeof(row->key));
    row->data = data;

    dsh_timeout_update(table, row);

    return DSH_OK;
}

void *dsh_lookup(dsh_t *table, dsh_key_t *key) {
    int64_t idx = _lookup(table, key);

    if(idx < 0)
        return NULL;

    dsh_timeout_old(table, table->timeout);

FIX

    dsh_row_t *row = table->rows[idx];

    if(!row->data)
        return NULL;

    dsh_timeout_update(table, row, idx);

    return row->data;
}

void dsh_clear(dsh_t *table, dsh_key_t *key) {
    int64_t idx = hash_func(table->num_rows, key);
    dsh_row_t *row = table->rows[idx];
    if(!row->data)
        return;

    // Collision
    if(!key_eq(key, &row->key)) {
        _clear_collision(row, key);
        dsh_lru_node_t *to = row->to_node;
        to->prev = to->next;
        free(tmp); // Remove this timeout node
    }

    _dsh_timeout_remove(table->timeouts, row->to_node);

    table->free_cb(row->data);
    table->stats.inserted--;
    row->data = NULL;
    row->deleted = true;
}

