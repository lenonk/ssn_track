
Blue Green Hash (bgh) is a solution for TCP/IP session tracking. Arbitrary data 
can be associated with a session and old sessions are automatically timedout.

The implementation is inspired by connection draining blue-green deployments.
BGH uses two threads, one of which controls a periodic refresh. During a 
refresh:

    * A new hash is allocated and is optionally sized to meet past resource 
      requirements
    * When a lookup is performed, and the data is found in the old table, it is
      automatically transitioned to the new table
    * All inserts go into the new table
    * After the timeout period, the old hash is destroyed. Any sessions 
      remaining in this hash are removed

Since hash reallocation and cleanup are performed in their own thread, and 
timeouts are performed on coarse blocks, the performance impact is negligible.

Used by https://github.com/ajkeeton/pack_stat for TCP session stats

# Building

    mkdir build ; cd build ; cmake .. ; make
    
# Basic usage

    bgh_new(...)
    bgh_insert(...)
    bgh_lookup(...)
    bgh_clear(...) - optional, as sessions are timed out automatically
    bgh_free(...)
 
Hashes must be provided with a callback to free data:

    void free_cb(void *data_to_free) { ... }

# Sample

    ./sample/pcap_stats <pcap>

# Configuring BGH

To use with defaults (see bgh.h), just provide bgh_new with a callback to free
the data you insert. This can not be null.

    bgh_new(free_cb)

For more control over BGH's behavior, pass in a bgh_config_t to bgh_config_new. 

Initialize a config to the defaults:

    bgh_config_t config;
    bgh_init_config(&config);

    // Seconds between refresh periods. 0 to disable refreshes and therefore 
    // also timeouts. Refreshes are necessary to properly clean up the table.
    config.refresh_period = 120; 
    // Seconds to wait during the refresh period for active sessions to 
    // transition. Anything left in the old hash after this timeout will be removed
    config.timeout = 30;
    // Initial number of rows. Should be prime
    config.initial_rows = 100003;
    // Lower bounds to shrink to. If 0, the initial size is used
    // Should be prime
    config.min_rows = 26003;
    // Max number of rows we can grow to
    // Should be prime
    config.max_rows = 15485867;
    // Inserts are ignored if the hash reaches this percentage full
    // It will be scaled up with the next refresh (if configured to do so)
    config.hash_full_pct = 8;
    // If the hash reaches this percent of inserts, it will be scaled up
    config.scale_up_pct = 5;
    // At this percentage, the hash will be scaled down
    config.scale_down_pct = 0.05;
    
    bgh_t *tracker = bgh_config_new(&config, free_cb);

# BGH Autoscaling

The number of inserts is tracked. If it reaches the scale_up_pct or 
scale_down_pct, the hash will be resized during the new refresh period.

Note, prime.cc contains a partial list of prime numbers. When scaling up or 
down, BGH selects the next prime in the list in the direction of scaling.

# Tests

To test, run:

    ./tests/test_bgh

# Benchmarks

On my Macbook, the total time for 8192 inserts, deletes, and 819200 lookups:

    - BGH: 40.855999 ms
    - STL map: 337.914001 ms

