
Building:

    mkdir build ; cd build ; cmake .. ; make

Test:

    ./tests/test

And for extra Sanity:

    valgrind --tool=memcheck tests/test

Sample:

    ./sample/pcap_stats <pcap>
