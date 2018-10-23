pycachesim
==========

A single-core cache hierarchy simulator written in python.

.. image:: https://travis-ci.org/RRZE-HPC/pycachesim.svg?branch=master
    :target: https://travis-ci.org/RRZE-HPC/pycachesim?branch=master

The goal is to accurately simulate the caching (allocation/hit/miss/replace/evict) behavior of all cache levels found in modern processors. It is developed as a backend to `kerncraft <https://github.com/RRZE-HPC/kerncraft>`_, but is also planned to introduce a command line interface to replay LOAD/STORE instructions.

Currently supported features:
 * Inclusive cache hierarchies
 * LRU, MRU, RR and FIFO policies 
 * N-way cache associativity
 * Write-allocate with write-back caches
 * Non-write-allocate with write-through caches
 * Write-combining with sub-blocking
 * Tracking of cacheline states (e.g., using dirty bits)
 * Speed (core is implemented in C)
 * Python 2.7+ and 3.4+ support, with no other dependencies

Planned features:
 * Report cachelines on all levels (preliminary support through ``backend.verbosity > 0``)
 * Report timeline of cache events (preliminary support through ``backend.verbosity > 0``)
 * Visualize events (html file?)
 * Interface to Valgrind Infrastructure (see `Lackey <http://valgrind.org/docs/manual/lk-manual.html>`_) for access history replay.
 * (uncertain) instruction cache
 * Optional classification into compulsory/capacity and conflict misses (by simulating other cache configurations in parallel)
 * (uncertain) multi-core support
 
License
-------

pycachesim is licensed under AGPLv3.

Usage
-----

.. code-block:: python

    from cachesim import CacheSimulator, Cache, MainMemory
    
    mem = MainMemory()
    l3 = Cache("L3", 20480, 16, 64, "LRU")  # 20MB: 20480 sets, 16-ways with cacheline size of 64 bytes
    mem.load_to(l3)
    mem.store_from(l3)
    l2 = Cache("L2", 512, 8, 64, "LRU", store_to=l3, load_from=l3)  # 256KB
    l1 = Cache("L1", 64, 8, 64, "LRU", store_to=l2, load_from=l2)  # 32KB
    cs = CacheSimulator(l1, mem)
    
    cs.load(2342)  # Loads one byte from address 2342, should be a miss in all cache-levels
    cs.store(512, length=8)  # Stores 8 bytes to addresses 512-519,
                             # will also be a load miss (due to write-allocate)
    cs.load(512, length=8)  # Loads from address 512 until (exclusive) 520 (eight bytes)
    
    cs.force_write_back()
    cs.print_stats()
    
This should return:

.. code-block:: python

    CACHE *******HIT******** *******MISS******* *******LOAD******* ******STORE*******
       L1      1 (       8B)      2 (      65B)      3 (      73B)      1 (       8B)
       L2      0 (       0B)      2 (     128B)      2 (     128B)      1 (      64B)
       L3      0 (       0B)      2 (     128B)      2 (     128B)      1 (      64B)
      MEM      2 (     128B)      0 (       0B)      2 (     128B)      1 (      64B)

Each row refers to one memory-level, starting with L1 and ending with main memory. The 3 loads in L1 are the sum of all individual accesses to the cache-hierarchy. 1 (from first load) + 1 (from store with write-allocate) + 1 (from second load) = 3.

The 1 hit, is for bytes which were cached already. Internally the pycachesim operates on cache-lines, which all addresses get transformed to. Thus, the two misses throughout all cache-levels are actually two complete cache-lines and after the cache-line had been loaded the consecutive access to the same cache-line are handled as hits. That is also the reason why data sizes increase from L1 to L2. L1 is accessed byte-wise and L2 only with cache-line granularity.

So: hits, misses, stores and loads in L1 are byte-wise. Every other statistical information are based on cache-lines.

When using victim caches, setting `victims_to` to the victim cache level, will cause pycachesim to forward unmodified cache-lines to this level on replacement. During a miss, victims_to is checked for availability and only hit if it the cache-line is found. This means, that load stats will equal hit stats in victim caches and misses should always be zero.

Comparison to other Cache Simulators
====================================

While searching for more versatile cache simulator for kerncraft, I stumbled across the following:

 * gem5_:
   Very fully-featured full system simulator. Complex to extract only the memory subsystem
 * dineroIV_:
   Nice and simple code, but does not support exclusive caches and not available under open source license.
 * cachegrind_:
   Maintained and stable code of a well established open source project, but only supports inclusive first and last level caches.
 * callgrind_:
   see cachegrind
 * SMPcache_:
   Only supports one single cache and runs on Windows with GUI. Also not freely available.
 * CMPsim_:
   Was only academically published and source code never made available.
 * CASPER_:
   Was only academically published and source code never made available.

=========== ================= =========== =============== ================= ======== ======== ========= ======= ======== ============== ============== =========== =============== ================= ===================================
Package     instructions [0]_ blocks [1]_ sub-blocks [2]_ associtivity [3]_ LRU [4]_ MRU [4]_ FIFO [4]_ RR [4]_ CCC [5]_ 3+ levels [6]_ exclusive [7]_ victim [8]_ multi-core [9]_ API [10]_         open source [11]_
=========== ================= =========== =============== ================= ======== ======== ========= ======= ======== ============== ============== =========== =============== ================= ===================================
gem5_              x              x             ?                x             x       x         x        ?       ?            x             ?             ?             ?         python, ruby, c++  yes, BSD-style    
dineroIV_          x              x             x                x             x                 x        x       x            x                                                   c                  no, free for non-comercial use    
cachegrind_        x              x                              x             x                                                                                                   cli                yes, GPLv2       
callgrind_         x              x                              x             x                                                                                                   cli                yes, GPLv2          
SMPcache_                         x                              x             x                 x        x       ?                                                                Windows GUI       no, free for education und research        
CMPsim_                           x                              x             x       x         x        x                    x             ?             ?             x         ?                  no, source not public         
CASPER_            x              x             x                x             x       x         x        x       x            x                                         x         perl, c            no, source not public        
pycachesim                        x             x                x             x       x         x        x                    x           x               x                       python, C backend  yes, AGPLv3          
=========== ================= =========== =============== ================= ======== ======== ========= ======= ======== ============== ============== =========== =============== ================= ===================================

.. _gem5: http://gem5.org/Main_Page
.. _dineroIV: http://pages.cs.wisc.edu/~markhill/DineroIV/
.. _cachegrind: http://valgrind.org/docs/manual/cg-manual.html
.. _callgrind: http://valgrind.org/docs/manual/cl-manual.html
.. _SMPcache: http://arco.unex.es/smpcache/
.. _CMPsim: http://eng.umd.edu/~blj/papers/mobs2008.pdf
.. _CASPER: http://ieeexplore.ieee.org/stamp/stamp.jsp?arnumber=1240655

.. [0] Instruction cache support (typically L1I)
.. [1] Cacheline/block granular caching
.. [2] Sub-blocking/sectoring for in cache-storage
.. [3] Support for n-way associativity
.. [4] Support least-recently-used (LRU), most-recently-used (MRU), first-in-last-out (FIFO), random (RR) replacement policy
.. [5] Classification of misses into: compulsory (first time access), capacity (access after replacement), conflict (would have been a hit with full-associativity)
.. [6] Combining of at least three cache levels
.. [7] Exclusive cache relations (two levels may not share the same cacheline)
.. [8] Victim caches, where only evicted lines endup(e.g., AMD Bulldozer L3)
.. [9] Multi-core cache hierarchies with private and shared caches and cache coherency protocol
.. [10] Supported interfaces (cli = command-line-interface)
.. [11] Published under an Open Source Initiative approved license?
