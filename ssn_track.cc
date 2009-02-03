#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "ssn_track.h"

int ssnt_log_level = SSNT_DEBUG;

void debug(const char *args, ...) {
    if(ssnt_log_level > SSNT_DEBUG)
        return;

    va_list ap;
    va_start(ap, args);
    printf("DEBUG: ");
    vprintf(args, ap);
    puts("");
    va_end(ap);
}

ssnt_row_t *ssnt_new_row() {
    ssnt_row_t *row = new ssnt_row_t;
    row->data = NULL;
    row->to_node = NULL;
    memset(&row->key, 0, sizeof(row->key));
    return row;
};

ssnt_t *ssnt_new(void (*free_cb)(void *)) {
    ssnt_t *table = new ssnt_t;
    table->num_rows = SSNT_DEFAULT_NUM_ROWS;
    table->timeout = SSNT_DEFAULT_TIMEOUT;
    table->inserted = 0;
    table->rows = new ssnt_row_t*[table->num_rows];
    for(int i=0; i<table->num_rows; i++)
        table->rows[i] = ssnt_new_row();
    table->timeouts = new ssnt_lru_t;
    table->timeouts->head = NULL;
    table->timeouts->tail = NULL;
    table->free_cb = free_cb;
    return table;
}

void ssnt_free(ssnt_t *table) {
    if(!table) return;

    for(ssnt_lru_node_t *node = table->timeouts->head; node; ) {
        printf("deleting to node %p\n", node);
        table->free_cb(table->rows[node->idx]->data);
        ssnt_lru_node_t *tmp = node; 
        node = node->next;
        delete tmp;
    }

    delete table->timeouts;

    for(int i=0; i<table->num_rows; i++)
        delete table->rows[i];

    delete []table->rows;
    delete table;
}

void ssnt_timeout_update(ssnt_t *table, ssnt_row_t *row, uint64_t idx) {
    ssnt_lru_t *timeouts = table->timeouts;
    ssnt_lru_node_t *node;

    // Null if new node
    if(!row->to_node) {
        node = new ssnt_lru_node_t;
        printf("Created to node %p for index %d\n", node, idx);
        node->next = node->prev = NULL;
        row->to_node = node;
        node->idx = idx;
    } else {
        node = row->to_node;
    }

    if(node != timeouts->head) {
        if(node->prev) {
            node->prev->next = node->next;
        }

        if(node->next) {
            node->next->prev = node->prev;
        }

        node->next = timeouts->head;
        node->prev = NULL;
        if(timeouts->head)
            timeouts->head->prev = node;
        timeouts->head = node;
    }

    node->last = time(NULL);

    if(!node->next)
        timeouts->tail = node;
    else {
        // Iterate backwards from tail, removing old nodes
        time_t now = time(NULL);

        ssnt_lru_node_t *oldest = timeouts->tail;
        while(oldest && oldest != node) {
            if(now - oldest->last < SSNT_DEFAULT_TIMEOUT) {
                break;
            }

            printf("Timing out node for idx %d\n", oldest->idx);
            if(oldest->prev) {
                oldest->prev->next = NULL;
            }
            // We're removing the head node
            else {
                timeouts->head = timeouts->tail = NULL;
            }

            row = table->rows[oldest->idx];
            if(row->data) {
                table->free_cb(row->data);
                table->inserted--;
                row->data = NULL;
            }

            ssnt_lru_node_t *tmp = oldest;
            oldest = oldest->prev;
            delete tmp;
        }

        timeouts->tail = oldest;
    }
}

void ssnt_timeout_remove(ssnt_lru_t *timeouts, ssnt_lru_node_t *node) {
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

    printf("Deleting TO node %p idx %d\n", node, node->idx);
    delete node;
}

static bool key_eq(ssnt_key_t *k1, ssnt_key_t *k2) {
    return !memcmp(k1, k2, sizeof(ssnt_key_t));
}

uint64_t hash_func(uint64_t mask, ssnt_key_t *key) {
    uint64_t h = key->sip ^ key->dip;
    h ^= key->sport ^ key->dport;
    return (h ^ key->vlan) % mask;
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
        if(idx >= table->num_rows)
            idx = 0;

        ssnt_row_t *row = table->rows[idx];

        if(!row->data || key_eq(key, &row->key))
            return idx;
    }

    return -1;
}

ssnt_stat_t ssnt_insert(ssnt_t *table, ssnt_key_t *key, void *data) {
    // null data is not allowed
    // data is used to check if a row is used
    if(!data)
        return SSNT_EXCEPTION;

    if(table->inserted * 4 > table->num_rows)
        return SSNT_FULL;

    table->inserted++;
    int64_t idx = _lookup(table, key);
    if(idx < 0)
        return SSNT_EXCEPTION;

    ssnt_row_t *row = table->rows[idx];

    if(row->data)
        table->free_cb(row);

    ssnt_timeout_update(table, row, idx);

    memcpy(&row->key, key, sizeof(row->key));
    row->data = data;
    return SSNT_OK;
}

void *ssnt_lookup(ssnt_t *table, ssnt_key_t *key) {
    int64_t idx = _lookup(table, key);

    if(idx < 0)
        return NULL;

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

    if(row->to_node)
        ssnt_timeout_remove(table->timeouts, row->to_node);

    table->free_cb(row->data);
    table->inserted--;
    row->data = NULL;
    row->to_node = NULL;
}

