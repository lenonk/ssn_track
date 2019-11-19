#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <assert.h>
#include <stdio.h>
#include <sys/time.h>
#include <map>
#include <list>
#include "ssn_track.h"

extern "C" void ssnt_debug_struct(ssnt_t *table);

void free_cb(void *p) {
    free(p);
}

void basic() {
    printf("%s\n", __func__);

    // 16 rows, 5 minute timeout
    ssnt_t *tracker = ssnt_new(16, 5*60, free_cb);

    ssnt_key_t key;
    // Bzero'ing to clean up pad bytes. Needed to prevent valgrind from complaining
    bzero(&key, sizeof(key)); 

    // Add three
    key = { 1, 2, 3, 4, 5};
    ssnt_insert(tracker, &key, strdup("foo"));
    key.sip = 2;
    ssnt_insert(tracker, &key, strdup("bar"));
    key.sip = 3;
    ssnt_insert(tracker, &key, strdup("baz"));

    // Lookup each
    char *data;
    key.sip = 1;
    data = (char*)ssnt_lookup(tracker, &key);
    assert(!strcmp(data, "foo"));

    key.sip = 2;
    data = (char*)ssnt_lookup(tracker, &key);
    assert(!strcmp(data, "bar"));

    key.sip = 3;
    data = (char*)ssnt_lookup(tracker, &key);
    assert(!strcmp(data, "baz"));

    key.sip = 2;
    ssnt_delete(tracker, &key);

    key.sip = 1;
    ssnt_delete(tracker, &key);
    assert(!ssnt_lookup(tracker, &key));
    // Already deleted
    ssnt_delete(tracker, &key);
    assert(!ssnt_lookup(tracker, &key));
    key.sip = 2;
    ssnt_delete(tracker, &key);
    assert(!ssnt_lookup(tracker, &key));
    key.sip = 3;
    ssnt_delete(tracker, &key);
    assert(!ssnt_lookup(tracker, &key));

    ssnt_free(tracker);
}

#define NUM_ITS 4096*2
void fuzz() {
    // Random adds, deletes, and timeouts
    printf("%s\n", __func__);

    // Make repeatable
    srand(1);
    ssnt_t *tracker = ssnt_new(SSNT_DEFAULT_NUM_ROWS, 1, free_cb);

    ssnt_key_t key;
    // Bzero'ing to clean up pad bytes and prevent valgrind from complaining
    bzero(&key, sizeof(key)); 
    key.dport = (uint16_t)rand();
    key.dip = rand();

    ssnt_key_t keys[NUM_ITS];
    memset(&keys, 0, sizeof(keys));

    for(int i=0; i<NUM_ITS; i++) {
        key.sip = rand();
        key.sport = (uint16_t)rand();
        keys[i] = key;

        assert(ssnt_insert(tracker, &key, strdup("foo")) == SSNT_OK);
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
            // assert(!strcmp((char*)ssnt_lookup(tracker, &keys[rand() % i]), "foo"));
            ssnt_lookup(tracker, &keys[rand() % i]);
        }

        if(tracker->stats.collisions > 5) {
            printf("Exceeded max number of collisions with %d inserted\n", 
                tracker->stats.inserted);
            abort();
        }
    }

    assert(tracker->stats.timeouts > 1);
    ssnt_debug_struct(tracker);

    puts("");
    ssnt_free(tracker);
}

void timeouts() {
    printf("%s\n", __func__);
    // 16 rows, 2 second timeout
    ssnt_t *tracker = ssnt_new(16, 2, free_cb);

    ssnt_key_t key;
    // Bzero'ing to clean up pad bytes and prevent valgrind from complaining
    bzero(&key, sizeof(key)); 
    
    // Add three
    key = { 1, 2, 3, 4, 5};
    ssnt_insert(tracker, &key, strdup("foo"));
    sleep(1);
    key.sip = 2;
    ssnt_insert(tracker, &key, strdup("bar"));
    key.sip = 3;
    ssnt_insert(tracker, &key, strdup("baz"));

    assert(tracker->stats.collisions == 0);

    puts("Forcing timeout");
    // Timeout oldest
    sleep(1);
    ssnt_timeout_old(tracker, 2);
    key.sip = 1;
    assert(!ssnt_lookup(tracker, &key));

    // Put it back
    ssnt_insert(tracker, &key, strdup("foo"));
    // Lookup each remaining
    key.sip = 2;
    assert(!strcmp((char*)ssnt_lookup(tracker, &key), "bar"));
    key.sip = 3;
    assert(!strcmp((char*)ssnt_lookup(tracker, &key), "baz"));

    ssnt_debug_struct(tracker);

    sleep(2);
    puts("Timing out remaining");
    // Timeout without doing an operation
    ssnt_timeout_old(tracker, 2);

    ssnt_debug_struct(tracker);

    assert(!ssnt_lookup(tracker, &key));
    key.sip = 2;
    assert(!ssnt_lookup(tracker, &key));

    // The lookups will have changed the placement in the LRU
    // Make sure the timeouts work as expected
   
    ssnt_free(tracker);
}

struct key_cmp {
    bool operator()(const ssnt_key_t &k1, const ssnt_key_t &k2) const {
        return memcmp((void*)&k1, (void*)&k2, sizeof(ssnt_key_t)) < 0;
    }
};

void bench_stl() {
    std::map<ssnt_key_t, char *, key_cmp> tree;
    std::list<ssnt_key_t> to;

    ssnt_key_t key;
    bzero(&key, sizeof(key)); 
    key.dport = (uint16_t)rand();
    key.dip = rand();

    ssnt_key_t keys[NUM_ITS];
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
    ssnt_t *tracker = ssnt_new(SSNT_DEFAULT_NUM_ROWS, 1, free_cb);

    ssnt_key_t key;
    // Bzero'ing to clean up pad bytes and prevent valgrind from complaining
    bzero(&key, sizeof(key)); 
    key.dport = (uint16_t)rand();
    key.dip = rand();

    ssnt_key_t keys[NUM_ITS];
    memset(&keys, 0, sizeof(keys));

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now = 1000000 * tv.tv_sec + tv.tv_usec;

    for(int i=0; i<NUM_ITS; i++) {
        key.sip = rand();
        key.sport = (uint16_t)rand();
        keys[i] = key;

        ssnt_insert(tracker, &key, strdup("foo"));
    }

    for(int i=0; i<NUM_ITS*10; i++) {
        uint64_t k = rand() % NUM_ITS;
        ssnt_lookup(tracker, &keys[k]);
    }

    for(int i=0; i<NUM_ITS; i++) 
        ssnt_delete(tracker, &keys[i]);

    gettimeofday(&tv, NULL);
    uint64_t fin = 1000000 * tv.tv_sec + tv.tv_usec;
    printf("%d inserts, %d lookups, and deletes: %f ms\n", NUM_ITS, NUM_ITS*10, float((fin - now))/1000);
    ssnt_free(tracker);

    bench_stl();
}

int main(int argc, char **argv) {
    basic();
    timeouts();
    bench();
    fuzz();
    // TODO: check hash distrib?
    return 0;
}
