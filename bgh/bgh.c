/*
 * @author  Adam Keeton <ajkeeton@gmail.com>
 * Copyright (C) 2009-2020 Adam Keeton
 * TCP session tracker, with timeouts. Uses a "blue-green" mechanism for 
 * timeouts and automatic hash resizing
*/

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "bgh.h"
#include "primes.h"

void bgh_config_init(bgh_config_t *config) {
    int len = prime_total();

    config->starting_rows = prime_at_idx(len/2); 
    config->min_rows = prime_at_idx(0);
    config->max_rows = prime_at_idx(len);
    config->timeout = BGH_DEFAULT_TIMEOUT;
    config->refresh_period = BGH_DEFAULT_REFRESH_PERIOD;
    config->hash_full_pct = BGH_DEFAULT_HASH_FULL_PCT;

    // Control scaling
    // If the number of inserts > number rows * scale_up_pct
    // Scale up
    config->scale_up_pct = BGH_DEFAULT_HASH_FULL_PCT * 0.75;
    // If the number of inserts < number rows * scale_down_pct
    // Scale down
    config->scale_down_pct = BGH_DEFAULT_HASH_FULL_PCT * 0.1;
}

bgh_row_t *bgh_new_row() {
    bgh_row_t *row = (bgh_row_t*)calloc(sizeof(bgh_row_t), 1);
    if(!row)
        return NULL;
    return row;
};

bgh_t *bgh_new(void (*free_cb)(void *)) {
    bgh_config_t config;
    bgh_config_init(&config);

    return bgh_config_new(&config, free_cb);
}

void bgh_free_table(bgh_tbl_t *tbl) {
    for(int i=0; i<tbl->num_rows; i++) {
        if(tbl->rows[i]->data) {
            tbl->free_cb(tbl->rows[i]->data);
        }
        free(tbl->rows[i]);
    }

    free(tbl->rows);
    free(tbl);
}

bgh_tbl_t *bgh_new_tbl(uint64_t rows, uint64_t max_inserts, void (*free_cb)(void *)) {
    bgh_tbl_t *tbl = (bgh_tbl_t*)malloc(sizeof(bgh_tbl_t));
    if(!tbl)
        return NULL;

    tbl->num_rows = rows;
    tbl->rows = (bgh_row_t**)malloc(sizeof(bgh_row_t) * tbl->num_rows);

    if(!tbl->rows) {
        free(tbl);
        return NULL;
    }

    for(int i=0; i<tbl->num_rows; i++)
        tbl->rows[i] = bgh_new_row();

    tbl->free_cb = free_cb;
    tbl->inserted = tbl->collisions = 0;
    tbl->max_inserts = max_inserts;
    return tbl;
}

uint64_t _update_size(bgh_config_t *config, int idx, bgh_tbl_t *tbl) {
    // TODO: incorporate a timeout

    uint64_t next = 0;
    if(config->scale_up_pct > 0 && (tbl->inserted > tbl->num_rows * config->scale_up_pct/100.0)) {
        next = prime_larger_idx(idx);
        if(next > config->max_rows)
            return config->max_rows;
        return next;
    }

    if(tbl->inserted < tbl->num_rows * config->scale_down_pct/100.0) {
        next = prime_smaller_idx(idx);
        if(next < config->min_rows)
            return config->min_rows;
        return next;
    }

    return tbl->num_rows;
}

static void *refresh_thread(void *ctx) {
    bgh_t *ssns = (bgh_t*)ctx;
    static time_t last = 0;

    last = time(NULL);

    int pindex = prime_nearest_idx(ssns->config.starting_rows);

    while(ssns->running) {
        time_t now = time(NULL);

        // See if we should begin building a new table yet
        if(now - last < ssns->config.refresh_period) {
            usleep(50000); // 50 ms
            continue;
        }

        last = now;

        // Calc new hash size
        uint64_t nrows = _update_size(&ssns->config, pindex, ssns->active);
        uint64_t max_inserts = nrows * ssns->config.hash_full_pct/100.0;

        // Create new hash
        ssns->standby = bgh_new_tbl(nrows, max_inserts, ssns->active->free_cb);

        if(!ssns->standby) {
            // XXX Need way to handle/report this case gracefully
            // For now, just skip resize + timneout :/
            continue;
        }

        ssns->refreshing = true;

        // When we're refreshing, all new sessions go into the new table
        // Lookups are tried on both, if the first lookup fails. When a 
        // lookup succeeds on the active (and about to be replaced) table, 
        // the data is removed from that table and inserted in the standby table
        sleep(ssns->config.timeout);

        bgh_tbl_t *old_tbl = ssns->active;

        // Swap to the new table
        pthread_mutex_lock(&ssns->lock);
        ssns->active = ssns->standby;
        ssns->refreshing = false;
        pthread_mutex_unlock(&ssns->lock);

        // Not technically necessary, but makes debugging easier
        ssns->standby = NULL;

        // Delete old table
        bgh_free_table(old_tbl);
    }

    return NULL;
}

bgh_t *bgh_config_new(bgh_config_t *config, void (*free_cb)(void *)) {
    bgh_t *table = (bgh_t*)malloc(sizeof(bgh_t));
    if(!table)
        return NULL;

    table->config = *config;

    table->active = bgh_new_tbl(
        config->starting_rows, 
        config->starting_rows * config->hash_full_pct/100.0, 
        free_cb);

    table->standby = NULL;

    if(config->refresh_period > 0)
        table->running = true;
    else
        table->running = false;

    table->refreshing = false;
    pthread_mutex_init(&table->lock, NULL);

    pthread_create(&table->refresh, NULL, refresh_thread, table);

    return table;
}

void bgh_free(bgh_t *ssns) {
    if(!ssns) return;

    ssns->running = false;
    pthread_join(ssns->refresh, NULL);

    bgh_free_table(ssns->active);
    if(ssns->standby)
        bgh_free_table(ssns->standby);
    free(ssns);
}

static inline int key_eq(bgh_key_t *k1, bgh_key_t *k2) {
    uint64_t *p1 = (uint64_t*)k1;
    uint64_t *p2 = (uint64_t*)k2;

    return 
        ((p1[0] == p2[0] && p1[1] == p1[1]) ||
        (p1[0] == p2[1] && p1[1] == p2[0])) &&
        k1->vlan == k2->vlan;
}

// Hash func: XOR32
// Reference: https://www.researchgate.net/publication/281571413_COMPARISON_OF_HASH_STRATEGIES_FOR_FLOW-BASED_LOAD_BALANCING
static inline uint64_t hash_func(uint64_t mask, bgh_key_t *key) {
    uint64_t h = ((uint64_t)(key->sip + key->dip) ^
                            (key->sport + key->dport));
    h *= 1 + key->vlan;
    return h % mask;
}

bool _try_heal_collision(bgh_tbl_t *table, int64_t idx) {
    // If previous row has been deleted, move this row's data up one.
    // This has the effect of gradually resolving collisions as data is
    // cleared.
    // The check for !idx just ignores an edge case rather than add
    // extra logic for it
    if(!idx || !table->rows[idx-1]->deleted)
        return false;

    table->rows[idx-1]->data = table->rows[idx]->data;
    table->rows[idx-1]->deleted = false;
    table->rows[idx]->deleted = true;
    table->rows[idx]->data = NULL;
    return true;
}

int64_t _lookup_idx(bgh_tbl_t *table, bgh_key_t *key) {
    int64_t idx = hash_func(table->num_rows, key);
    bgh_row_t *row = table->rows[idx];

    // If nothing is/was stored here, just return it anyway.
    // We'll check later
    // The check for "deleted" is to deal with the case where there was 
    // previously a collision
    if((!row->data && !row->deleted) || key_eq(key, &row->key))
        return idx;

    // There was a collision. Use linear probing
    //  NOTE: 
    //  - while draining or on _clear, we set data to null
    //  - if there had been a collision, we still need to be able to reach the
    //    collided node
    //  - "deleted" is used to handle that case

    uint64_t collisions = 0;
    uint64_t start = idx++;
    while(idx != start) {
        collisions++;

        if(idx >= table->num_rows)
            idx = 0;

        bgh_row_t *row = table->rows[idx];

        if(key_eq(key, &row->key)) {
            if(_try_heal_collision(table, idx)) {
                table->collisions--;
                idx--;
            }

            // Intentionally ignoring the collision count here. Otherwise, we 
            // wind up counting extra collisions every time we look up this row
            return idx;
        }

        if(!row->data && !row->deleted) {
            table->collisions += collisions;
            return idx;
        }

        idx++;
    }

    return -1;
}

bgh_row_t *_lookup_row(bgh_tbl_t *table, bgh_key_t *key) {
    int64_t idx = hash_func(table->num_rows, key);
    bgh_row_t *row = table->rows[idx];

    if(key_eq(key, &row->key))
        return row;

    // If nothing is/was stored here, just return it anyway.
    // We'll check later
    // The check for 'deleted' is to handle the case where
    // a collided row was moved
    if(!row->data && !row->deleted)
        return row;

    uint64_t start = idx++;
    while(idx != start) {
        if(idx >= table->num_rows)
            idx = 0;

        bgh_row_t *row = table->rows[idx];

        if(key_eq(key, &row->key)) {
            _try_heal_collision(table, idx);

            // Intentionally ignoring the collision count here. Otherwise, we 
            // wind up counting extra collisions every time we look up this row
            return row;
        }

        if(!row->data && !row->deleted) {
            return row;
        }

        idx++;
    }

    return NULL;
}
bgh_stat_t bgh_insert_table(bgh_tbl_t *tbl, bgh_key_t *key, void *data) {
    // XXX Handle this case better ...
    // - should allow overwrites
    // - use to influence the size of the next hash table
    if(tbl->inserted > tbl->max_inserts)
        return BGH_FULL;

    int64_t idx = _lookup_idx(tbl, key);

    if(idx < 0)
        return BGH_EXCEPTION;

    bgh_row_t *row = tbl->rows[idx];

    // If there was something there already, free it and overwrite
    if(row->data)
        tbl->free_cb(row->data);
    else
        tbl->inserted++;

    row->deleted = false;
    memcpy(&row->key, key, sizeof(row->key));
    row->data = data;
    return BGH_OK;
}

bgh_stat_t bgh_insert(bgh_t *ssns, bgh_key_t *key, void *data) {
    // null data is not allowed
    // data is used to check if a row is used
    if(!data)
        return BGH_EXCEPTION;

    pthread_mutex_lock(&ssns->lock);
    if(ssns->refreshing) {
        bgh_stat_t retval = bgh_insert_table(ssns->standby, key, data);
        pthread_mutex_unlock(&ssns->lock);
        return retval;
    }
    pthread_mutex_unlock(&ssns->lock);
    return bgh_insert_table(ssns->active, key, data);
}

static inline void _move_tables(
        bgh_tbl_t *active, bgh_tbl_t *standby, bgh_key_t *key, bgh_row_t *row) {
    bgh_insert_table(standby, key, row->data);
    active->inserted--;
    row->data = NULL;
    row->deleted = true; // this is necessary to handle the case where there 
                         // was a previous collision with this row
}

void *_draining_lookup_active(
        bgh_tbl_t *active, bgh_tbl_t *standby, bgh_key_t *key) {
    bgh_row_t *row = _lookup_row(active, key);
    if(!row || !row->data) {
        row = _lookup_row(standby, key);
        if(row) 
            return row->data;
        return NULL;
    }

    void *data = row->data;
    _move_tables(active, standby, key, row);
    return data;
}

void *_draining_prefer_standby(
        bgh_tbl_t *active, bgh_tbl_t *standby, bgh_key_t *key) {
    bgh_row_t *row = _lookup_row(standby, key);
    if(row && row->data) {
        return row->data;
    }

    row = _lookup_row(active, key);
    if(!row || !row->data)
        return NULL;

    void *data = row->data;
    _move_tables(active, standby, key, row);
    return data;
}

void *bgh_lookup(bgh_t *ssns, bgh_key_t *key) {
    void *data = NULL;

    pthread_mutex_lock(&ssns->lock);
    if(ssns->refreshing) {
        if(ssns->active->inserted > ssns->standby->inserted)
            data = _draining_lookup_active(ssns->active, ssns->standby, key);
        else 
            data = _draining_prefer_standby(ssns->active, ssns->standby, key);
        pthread_mutex_unlock(&ssns->lock);

        return data;
    } 
    pthread_mutex_unlock(&ssns->lock);
        
    bgh_row_t *row = _lookup_row(ssns->active, key);
    if(row)
        return row->data;
    return NULL;
}

void bgh_delete_from_table(bgh_tbl_t *table, bgh_key_t *key) {
    int64_t idx = _lookup_idx(table, key);
    if(idx < 0)
        return; // Should never happen

    bgh_row_t *row = table->rows[idx];

    if(!row->data) 
        return;

    table->free_cb(row->data);
    table->inserted--;
    row->data = NULL;
    row->deleted = true;
}

void bgh_clear(bgh_t *ssns, bgh_key_t *key) {
    pthread_mutex_lock(&ssns->lock);
    if(ssns->refreshing) {
        // XXX Revisit: Not optimal to just do both this way, but this is an edge case
        bgh_delete_from_table(ssns->active, key);
        bgh_delete_from_table(ssns->standby, key);
        pthread_mutex_unlock(&ssns->lock);
        return;
    }
    pthread_mutex_unlock(&ssns->lock);
    bgh_delete_from_table(ssns->active, key);
}
