pycachesim
==========

A single-core cache hierarchy simulator written in python.

.. image:: https://travis-ci.org/cod3monk/pycachesim.svg?branch=master
    :target: https://travis-ci.org/cod3monk/pycachesim?branch=master

The goal is to accurately simulate the caching (allocation/hit/miss/replace/evict) behavior of all cache levels found in modern processors. It is developed as a backend to `kerncraft <https://github.com/RRZE-HPC/kerncraft>`_, but is also planned to introduce a command line interface to replay LOAD/STORE instructions.

..
    This requires:
        * implementation of cache replacement strategies (foremost LRU)
        * implementation of associativity rules (e.g. full-associative, 2-way associativity)
        * rules to define the interaction between memory levels (e.g. inclusive, write-allocate)
    
    Features:
        * take a memory access stream and report statistics
        * operate on absolute and relative (offset) memory addresses
        * ignore a warm-up phase (bringing the simulator into a steady state)
    
    Possible features:
        * report timeline of events
        * Instruction caching (planned are only data caches)
    