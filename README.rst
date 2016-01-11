pycachesim
==========

A single-core cache hierarchy simulator written in python.

.. image:: https://travis-ci.org/RRZE-HPC/pycachesim.svg?branch=master
    :target: https://travis-ci.org/RRZE-HPC/pycachesim?branch=master

The goal is to accurately simulate the caching (allocation/hit/miss/replace/evict) behavior of all cache levels found in modern processors. It is developed as a backend to `kerncraft <https://github.com/RRZE-HPC/kerncraft>`_, but is also planned to introduce a command line interface to replay LOAD/STORE instructions.

Current features:
 * Inclusive cache hierarchies
 * LRU, MRU, RR and FIFO policies supported
 * Support for cache associativity
 * Optional write-allocate support
 * Speed (core is implemented in C)
 * Python 2.7+ and 3.4+ support, with no other dependencies

Planned features:
 * Rules to define the interaction between cache levels (e.g., exclusive caches, copy-back,...)
 * Report timeline of cache events
 * More detailed store/evict handling (e.g., using dirty bits)
 * (uncertain) instruction cache
 
License
-------

pycachesim is licensed under AGPLv3.

Usage
-----

.. code-block:: python

    from cachesim import CacheSimulator, Cache
    
    cacheline_size = 64
    l3 = Cache(20480, 16, cacheline_size, "LRU")  # 20MB 16-ways
    l2 = Cache(512, 8, cacheline_size, "LRU", parent=l3)  # 256kB 8-ways
    l1 = Cache(64, 8, cacheline_size, "LRU", parent=l2)  # 32kB 8-ways
    cs = CacheSimulator(l1, write_allocate=True)
    
    cs.load(2342)  # Loads one byte from address 2342, should be a miss in all cache-levels
    cs.store(512, length=8)  # stores 8 bytes to addresses 512-519,
                                     # will also be a load miss (due to write-allocate)
    cs.load(512, 520)  # Loads from address 512 until (exclusive) 520 (eight bytes)
    
    print(list(cs.stats()))
    
This should return:

.. code-block:: python

    [{u'LOAD': 17L, u'MISS': 2L, u'HIT': 15L, u'STORE': 8L},
     {u'LOAD': 2L, u'MISS': 2L, u'HIT': 0L, u'STORE': 8L},
     {u'LOAD': 2L, u'MISS': 2L, u'HIT': 0L, u'STORE': 8L}]

Each dictionary refers to one cache-level, starting with L1. The 17 loads are the sum of all byte-wise access to the cache-hierarchy. 1 (from first load) +8 (from store with write-allocate) +8 (from second load) = 17.

The 15 hits, are for bytes which were cached already. The high number is due to the byte-wise operation of the interface, so 15 bytes were already present in cache. Internally the pycachesim operates on cache-lines, which all addresses get transformed to. Thus, the two misses throughout all cache-levels are actually two complete cache-lines and after the cache-line had been loaded the consecutive access to the same cache-line are handled as hits.

So: hits and loads in L1 are byte-wise, just like stores throughout all cache-levels. Every other statistical information are based on cache-lines.
