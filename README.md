
Basic hash-table based TCP/IP session tracking with last-recently-used timeouts. Arbitrary user data can be associated with each session.

Used by https://github.com/ajkeeton/pack_stat for TCP session stats

Building:

    mkdir build ; cd build ; cmake .. ; make
    
Basic usage:

    ssnt_new(...)
    ssnt_insert(...)
    ssnt_lookup(...)
    ssnt_free(...)
    
Sample:

    ./sample/pcap_stats <pcap>

Note, for a more interesting use case, see https://github.com/ajkeeton/pack_stat/tree/master/sample

Test:

    ./tests/test

And for extra Sanity:

    valgrind --tool=memcheck tests/test

