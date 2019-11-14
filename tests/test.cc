#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <assert.h>
#include <stdio.h>
#include "ssn_track.h"

void ssnt_debug_struct(ssnt_t *table);

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

#define NUM_ITS 4096 
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
            printf("Exceeded max number of collisions after %d inserts\n", 
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

    puts("Forcing timeout");
    // Timeout oldest
    sleep(1);
    ssnt_timeout_update(tracker, 2);
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
    ssnt_timeout_update(tracker, 2);

    ssnt_debug_struct(tracker);

    assert(!ssnt_lookup(tracker, &key));
    key.sip = 2;
    assert(!ssnt_lookup(tracker, &key));

    // The lookups will have changed the placement in the LRU
    // Make sure the timeouts work as expected
   
    ssnt_free(tracker);
}

void bench() {
    printf("%s\n", __func__);
    puts("TODO");
}

int main(int argc, char **argv) {
    basic();
    timeouts();
    bench();
    fuzz();
    // TODO: check hash distrib?
    return 0;
}
