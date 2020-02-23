#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <assert.h>
#include <stdio.h>
#include <sys/time.h>
#include <map>
#include <list>
#include "../dsh/dsh.h"

extern "C" void dsh_debug_struct(dsh_t *table);
extern dsh_config_t dsh_config;

void free_cb(void *p) {
    free(p);
}

void basic() {
    printf("%s\n", __func__);

    // 24 rows, 5 minute timeout
    dsh_t *tracker = dsh_new(24, 5*60, free_cb);

    dsh_key_t key;
    // Bzero'ing to clean up pad bytes. Needed to prevent valgrind from complaining
    bzero(&key, sizeof(key)); 

    // (ip, ip, port, port, vlan)
    key = { 10, 200, 3000, 4000, 5};
    // Add three (changing source IP)
    dsh_insert(tracker, &key, strdup("foo"));
    key.sip = 20;
    dsh_insert(tracker, &key, strdup("bar"));
    key.sip = 30;
    dsh_insert(tracker, &key, strdup("baz"));

    // Lookup each
    char *data;
    key.sip = 10;
    data = (char*)dsh_lookup(tracker, &key);
    assert(!strcmp(data, "foo"));

    key.sip = 20;
    data = (char*)dsh_lookup(tracker, &key);
    assert(!strcmp(data, "bar"));

    // Confirm overwrite
    dsh_insert(tracker, &key, strdup("barbarbar"));
    data = (char*)dsh_lookup(tracker, &key);
    assert(!strcmp(data, "barbarbar"));

    key.sip = 30;
    data = (char*)dsh_lookup(tracker, &key);
    assert(!strcmp(data, "baz"));

    // Swap source and IP, should get same session data
    dsh_key_t key2 = { 200, 30, 4000, 3000, 5};
    data = (char*)dsh_lookup(tracker, &key2);
    assert(!strcmp(data, "baz"));

    key.sip = 20;
    dsh_delete(tracker, &key);

    key.sip = 10;
    dsh_delete(tracker, &key);
    assert(!dsh_lookup(tracker, &key));
    // Already deleted
    dsh_delete(tracker, &key);
    assert(!dsh_lookup(tracker, &key));
    key.sip = 20;
    dsh_delete(tracker, &key);
    assert(!dsh_lookup(tracker, &key));
    key.sip = 30;
    dsh_delete(tracker, &key);
    assert(!dsh_lookup(tracker, &key));

    dsh_free(tracker);
}

#define NUM_ITS 100 // 4096*2
void fuzz() {
    // Random adds, deletes, and timeouts
    printf("%s\n", __func__);

    // Make repeatable
    srand(1);
    dsh_t *tracker = dsh_new(DSH_DEFAULT_NUM_ROWS, 1, free_cb);

    dsh_key_t key;
    // Bzero'ing to clean up pad bytes and prevent valgrind from complaining
    bzero(&key, sizeof(key)); 
    key.dport = (uint16_t)rand();
    key.dip = rand();

    dsh_key_t keys[NUM_ITS];
    memset(&keys, 0, sizeof(keys));

    for(int i=0; i<NUM_ITS; i++) {
        key.sip = rand();
        key.sport = (uint16_t)rand();
        keys[i] = key;

        assert(dsh_insert(tracker, &key, strdup("foo")) == DSH_OK);
        usleep(5000); // Sleeping to help exercise the timeout code later
        if(!(i % 5)) {
            printf(".");
            fflush(stdout);
        }

        if(!(rand() % 3)) {
            // Pick random key out of the hat for a lookup

            // No way to know which was timeed out ATM, so just ignore the 
            // result and rely on valgrind to catch any issues for now
            // This forces the timeout code to rearrange the LRU
            // assert(!strcmp((char*)dsh_lookup(tracker, &keys[rand() % i]), "foo"));
            dsh_lookup(tracker, &keys[rand() % i]);
        }

        if(tracker->stats.collisions > 5) {
            printf("Exceeded max number of collisions with %d inserted\n", 
                tracker->stats.inserted);
            abort();
        }
    }

    // Just do a number of random lookups
    for(int i=0; i<NUM_ITS*5; i++) {
        int k = rand() % (sizeof(keys)/sizeof(keys[0]));

        if(!(i % 5)) {
            printf(".");
            fflush(stdout);
        }

        dsh_lookup(tracker, &keys[k]);
    }

    // Make sure everything gets timed out naturally
    sleep(1);
    int k = rand() % (sizeof(keys)/sizeof(keys[0]));
    dsh_lookup(tracker, &keys[k]);
    dsh_debug_struct(tracker);

    // Timeouts only happen on updates, so will always be 1 behind
    assert(tracker->stats.timeouts == NUM_ITS);
    dsh_free(tracker);
}

void timeouts() {
    printf("%s\n", __func__);
    // 16 rows, 2 second timeout
    dsh_t *tracker = dsh_new(16, 2, free_cb);

    dsh_key_t key;
    // Bzero'ing to clean up pad bytes and prevent valgrind from complaining
    bzero(&key, sizeof(key)); 
    
    // Add three
    key = { 1, 2, 3, 4, 5};
    dsh_insert(tracker, &key, strdup("foo"));
    sleep(1);
    key.sip = 2;
    dsh_insert(tracker, &key, strdup("bar"));
    key.sip = 3;
    dsh_insert(tracker, &key, strdup("baz"));

    assert(tracker->stats.collisions == 0);

    puts("Forcing timeout");
    // Timeout oldest
    sleep(1);
    dsh_timeout_old(tracker, 2);
    key.sip = 1;
    assert(!dsh_lookup(tracker, &key));

    // Put it back
    dsh_insert(tracker, &key, strdup("foo"));
    // Lookup each remaining
    key.sip = 2;
    assert(!strcmp((char*)dsh_lookup(tracker, &key), "bar"));
    key.sip = 3;
    assert(!strcmp((char*)dsh_lookup(tracker, &key), "baz"));

    dsh_debug_struct(tracker);

    sleep(2);
    puts("Timing out remaining");
    // Timeout without doing an operation
    dsh_timeout_old(tracker, 2);

    dsh_debug_struct(tracker);

    assert(!dsh_lookup(tracker, &key));
    key.sip = 2;
    assert(!dsh_lookup(tracker, &key));

    // The lookups will have changed the placement in the LRU
    // Make sure the timeouts work as expected
   
    dsh_free(tracker);
}

struct key_cmp {
    bool operator()(const dsh_key_t &k1, const dsh_key_t &k2) const {
        return memcmp((void*)&k1, (void*)&k2, sizeof(dsh_key_t)) < 0;
    }
};

void bench_stl() {
    std::map<dsh_key_t, char *, key_cmp> tree;
    std::list<dsh_key_t> to;

    dsh_key_t key;
    bzero(&key, sizeof(key)); 
    key.dport = (uint16_t)rand();
    key.dip = rand();

    dsh_key_t keys[NUM_ITS];
    memset(&keys, 0, sizeof(keys));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now = 1000000 * tv.tv_sec + tv.tv_usec;

    for(int i=0; i<NUM_ITS; i++) {
        key.sip = rand();
        key.sport = (uint16_t)rand();
        keys[i] = key;
        tree[key] = strdup("foo");
        to.push_front(key);
    }

    for(int i=0; i<NUM_ITS*10; i++) {
        uint64_t k = rand() % NUM_ITS;
        auto it = tree.find(keys[k]);
        // assert(it != tree.end());

        // TODO: real simulation? Need to find actual node and move it
        to.pop_front();
        to.push_front(key);
    }

    for(int i=0; i<NUM_ITS; i++) {
        auto it = tree.find(keys[i]);
        free(it->second);
        tree.erase(keys[i]);
        to.pop_front();
    }

    gettimeofday(&tv, NULL);
    uint64_t fin = 1000000 * tv.tv_sec + tv.tv_usec;
    printf("STL map (*without overhead from timeouts*): "
            "%d inserts, lookups, and deletes: %f ms\n", 
            NUM_ITS, float((fin - now))/1000);
}

void bench() {
    printf("%s\n", __func__);

    // Make repeatable
    srand(1);
    dsh_t *tracker = dsh_new(DSH_DEFAULT_NUM_ROWS, 1, free_cb);

    dsh_key_t key;
    // Bzero'ing to clean up pad bytes and prevent valgrind from complaining
    bzero(&key, sizeof(key)); 
    key.dport = (uint16_t)rand();
    key.dip = rand();

    dsh_key_t keys[NUM_ITS];
    memset(&keys, 0, sizeof(keys));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now = 1000000 * tv.tv_sec + tv.tv_usec;

    for(int i=0; i<NUM_ITS; i++) {
        key.sip = rand();
        key.sport = (uint16_t)rand();
        keys[i] = key;

        dsh_insert(tracker, &key, strdup("foo"));
    }

    for(int i=0; i<NUM_ITS*10; i++) {
        uint64_t k = rand() % NUM_ITS;
        dsh_lookup(tracker, &keys[k]);
    }

    for(int i=0; i<NUM_ITS; i++) 
        dsh_delete(tracker, &keys[i]);

    gettimeofday(&tv, NULL);
    uint64_t fin = 1000000 * tv.tv_sec + tv.tv_usec;
    printf("%d inserts, %d lookups, and deletes: %f ms\n", NUM_ITS, NUM_ITS*10, float((fin - now))/1000);
    dsh_free(tracker);

    bench_stl();
}

int main(int argc, char **argv) {
    dsh_config.log_level = DSH_DEBUG;
    basic();
    timeouts();
    fuzz();
    bench();
    // TODO: check hash distrib?
    return 0;
}
