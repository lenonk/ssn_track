/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2019 Adam Keeton
 * TCP session tracker, with timeouts. Uses a "blue-green" mechanism for 
 * timeouts and automatic hash resizing
*/

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <unistd.h>
#include "ssn_track_hd.h"

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

#if 0
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
#endif
}

ssnt_row_t *ssnt_new_row() {
    ssnt_row_t *row = (ssnt_row_t*)calloc(sizeof(ssnt_row_t), 1);
    if(!row)
        return NULL;
    return row;
};

ssnt_t *ssnt_new_defaults(void (*free_cb)(void *)) {
    return ssnt_new(SSNT_DEFAULT_NUM_ROWS, SSNT_DEFAULT_TIMEOUT, free_cb);
}

void ssnt_free_table(ssnt_tbl_t *tbl) {
    for(int i=0; i<tbl->num_rows; i++) {
        if(tbl->rows[i]->data) {
            tbl->free_cb(tbl->rows[i]->data);
        }
        free(tbl->rows[i]);
    }

    free(tbl->rows);
}

ssnt_tbl_t *ssnt_new_tbl(uint64_t rows, void (*free_cb)(void *)) {
    ssnt_tbl_t *tbl = (ssnt_tbl_t*)malloc(sizeof(ssnt_tbl_t));
    if(!tbl)
        return NULL;

    tbl->num_rows = rows;
    tbl->rows = (ssnt_row_t**)malloc(sizeof(ssnt_row_t) * tbl->num_rows);

    if(!tbl->rows) {
        free(tbl);
        return NULL;
    }

    for(int i=0; i<tbl->num_rows; i++)
        tbl->rows[i] = ssnt_new_row();

    tbl->free_cb = free_cb;
    tbl->inserted = tbl->collisions = 0;
}

static void *refresh_thread(void *ctx) {
    ssnt_t *ssns = (ssnt_t*)ctx;
    static time_t last = 0;

    last = time(NULL);

    while(ssns->running) {
        time_t now = time(NULL);

        // See if we should begin building a new table yet
        if(now - last < ssns->refresh_period) {
            usleep(50000); // 50 ms
            continue;
        }

        last = now;

        // Calc desired new num rows
        // nrows = update_size(...)
        // TODO: need prime num list, currently using old tbl size
        uint64_t nrows = ssns->active->num_rows;

        // Create new hash
        ssns->standby = ssnt_new_tbl(nrows, ssns->active->free_cb);
        if(!ssns->standby) {
            // XXX Need way to handle this case gracefully
            abort();
        }

        ssns->refreshing = true;
        // When we're refreshing, all new sessions go into the new table
        // Lookups are tried on both, if the first lookup fails. When a 
        // lookup succeeds on the active (and about to be replaced) table, 
        // the data is removed from that table and inserted in the standby table
        sleep(ssns->timeout);

        ssnt_tbl_t *old_tbl = ssns->active;

        // Swap to the new table
        pthread_mutex_lock(&ssns->lock);
        ssns->active = ssns->standby;
        ssns->refreshing = false;
        pthread_mutex_unlock(&ssns->lock);

        // Not technically necessary, but makes debugging easier
        ssns->standby = NULL;

        // Delete old table
        ssnt_free_table(old_tbl);
    }

    return NULL;
}

ssnt_t *ssnt_new(uint64_t rows, uint32_t timeout_seconds, void (*free_cb)(void *)) {
    ssnt_t *table = (ssnt_t*)malloc(sizeof(ssnt_t));
    if(!table)
        return NULL;

    table->refresh_period = SSNT_DEFAULT_REFRESH_PERIOD;
    table->timeout = SSNT_DEFAULT_TIMEOUT;

    table->active = ssnt_new_tbl(rows, free_cb);
    table->standby = NULL;

    table->running = true;
    pthread_mutex_init(&table->lock, NULL);
    pthread_create(&table->refresh, NULL, refresh_thread, table);
    return table;
}

void ssnt_free(ssnt_t *ssns) {
    if(!ssns) return;

    ssns->running = false;
    pthread_join(ssns->refresh, NULL);

    ssnt_free_table(ssns->active);
    if(ssns->standby)
        ssnt_free_table(ssns->standby);
    free(ssns);
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

static int64_t _lookup(ssnt_tbl_t *table, ssnt_key_t *key) {
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

        table->collisions++;

        if(idx >= table->num_rows)
            idx = 0;

        ssnt_row_t *row = table->rows[idx];

        if(!row->data || key_eq(key, &row->key))
            return idx;

        idx++;
    }

    return -1;
}

ssnt_stat_t ssnt_insert_table(ssnt_tbl_t *tbl, ssnt_key_t *key, void *data) {
    // XXX Handle this case better ...
    //   logic differs when refreshing, since new table maybe larger
    //   Use to influence the size of the next hash table
    if(tbl->inserted * 8 > tbl->num_rows) {
        tbl->collisions++;
        return SSNT_FULL;
    }

    int64_t idx = _lookup(tbl, key);
    if(idx < 0)
        return SSNT_EXCEPTION;

    ssnt_row_t *row = tbl->rows[idx];

    // If there was something there already, free it and overwrite
    if(row->data) {
        tbl->free_cb(row);
    }

    memcpy(&row->key, key, sizeof(row->key));
    tbl->inserted++;
    row->data = data;

    return SSNT_OK;
}

ssnt_stat_t ssnt_insert(ssnt_t *ssns, ssnt_key_t *key, void *data) {
    // null data is not allowed
    // data is used to check if a row is used
    if(!data)
        return SSNT_EXCEPTION;

    pthread_mutex_lock(&ssns->lock);
    if(ssns->refreshing) {
        ssnt_stat_t retval = ssnt_insert_table(ssns->standby, key, data);
        pthread_mutex_unlock(&ssns->lock);
        return retval;
    }
    pthread_mutex_unlock(&ssns->lock);
    return ssnt_insert_table(ssns->active, key, data);
}

void *_draining_active(ssnt_tbl_t *active, ssnt_tbl_t *standby, ssnt_key_t *key) {
    void *data = NULL;

    // Check the old (active) hash first
    // If found, move to the standby hash
    int64_t idx = _lookup(active, key);

    if(idx >= 0) {
        // Found idx in standby hash. If there's data there, move to 
        // standby hash
        data = active->rows[idx]->data;

        if(data) {
            // Found something, Move it to standby hash
            active->rows[idx]->data = NULL;
            active->inserted--;
            ssnt_insert_table(standby, data, key);
            return data;
        }
    }

    // No valid idx or null entry
    // Check the standby table in case it has already moved over
    idx = _lookup(standby, key);
    if(idx < 0)
        return NULL;

    return standby->rows[idx]->data;
}


void *_draining_standby(ssnt_tbl_t *active, ssnt_tbl_t *standby, ssnt_key_t *key) {
    void *data = NULL;

    // Check the on-deck (standby) table first
    int64_t idx = _lookup(standby, key);

    if(idx >= 0) {
        data = standby->rows[idx]->data;
        if(data) {
            // Found the data right where it should be
            return data;
        }
    }

    idx = _lookup(active, key);

    if(idx >= 0) {
        // Found idx in standby hash. If there's data there, move to 
        // standby hash
        data = active->rows[idx]->data;

        if(data) {
            active->rows[idx]->data = NULL;
            active->inserted--;
            ssnt_insert_table(standby, data, key);
            return data;
        }
    }

    return NULL;
}

void *ssnt_lookup(ssnt_t *ssns, ssnt_key_t *key) {
    int64_t idx = -1;
    void *data = NULL;

    pthread_mutex_lock(&ssns->lock);
    if(ssns->refreshing) {
        if(ssns->active->inserted > ssns->standby->inserted)
            data = _draining_active(ssns->active, ssns->standby, key);
        else 
            data = _draining_standby(ssns->active, ssns->standby, key);
        pthread_mutex_unlock(&ssns->lock);
        return data;
    } 
    pthread_mutex_unlock(&ssns->lock);
        
    idx = _lookup(ssns->active, key);

    if(idx < 0)
        return NULL;

    return ssns->active->rows[idx]->data;
}

void ssnt_delete_from_table(ssnt_tbl_t *table, ssnt_key_t *key) {
    int64_t idx = _lookup(table, key);
    if(idx < 0)
        return; // Should never happen

    ssnt_row_t *row = table->rows[idx];

    if(!row->data) 
        return;

    table->free_cb(row->data);
    table->inserted--;
    row->data = NULL;
}
void ssnt_delete(ssnt_t *ssns, ssnt_key_t *key) {
    pthread_mutex_lock(&ssns->lock);
    if(ssns->refreshing) {
        // XXX Revisit: Not optimal to just do both this way, but this is an edge case
        ssnt_delete_from_table(ssns->active, key);
        ssnt_delete_from_table(ssns->standby, key);
        pthread_mutex_unlock(&ssns->lock);
        return;
    }
    pthread_mutex_unlock(&ssns->lock);
    ssnt_delete_from_table(ssns->active, key);
}
