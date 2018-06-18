#!/usr/bin/env python3
"""Example cache hierarchies that can be simulated."""
from cachesim import Cache, MainMemory, CacheSimulator, CacheVisualizer

# =====================
# Intel Inclusive Cache
# =====================
cacheline_size = 64

mem = MainMemory()

l3 = Cache(name="L3",
           sets=20480, ways=16, cl_size=cacheline_size,
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=None, load_from=None, victims_to=None,
           swap_on_load=False)
mem.load_to(l3)
mem.store_from(l3)

l2 = Cache(name="L2",
           sets=512, ways=8, cl_size=cacheline_size,
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=l3, load_from=l3, victims_to=None,
           swap_on_load=False)

l1 = Cache(name="L1",
           sets=64, ways=8, cl_size=cacheline_size,
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=l2, load_from=l2, victims_to=None,
           swap_on_load=False)  # inclusive/exclusive does not matter in first-level

cs = CacheSimulator(first_level=l1, main_memory=mem)

cs.load(23)
cv = CacheVisualizer(cs, [10, 16])
cv.dump_state()

# =============================
# AMD Bulldozer Exclusive Cache
# =============================
cacheline_size = 64

mem = MainMemory()
l3 = Cache(name="L3",
           sets=2048, ways=64, cl_size=cacheline_size,  # 4MB
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=None, load_from=None, victims_to=None,
           swap_on_load=False)  # This is a victim cache, so exclusiveness should be obvious
mem.store_from(l3)
l2 = Cache(name="L2",
           sets=2048, ways=16, cl_size=cacheline_size,  # 2048kB
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=l3, load_from=None, victims_to=l3,
           swap_on_load=False)  # L2-L1 is inclusive (unlike with AMD Istanbul)
mem.load_to(l2)
wcc = Cache(name="WCC",
            sets=1, ways=64, cl_size=cacheline_size,  # 4KB
            subblock_size=1,  # sub-blocks of 1 Byte size
            replacement_policy="LRU",
            write_back=True, write_allocate=False,  # this policy only makes sens with WCC
            store_to=l2, load_from=None, victims_to=None,
            swap_on_load=False)  # inclusive/exclusive does not matter in first-level
l1 = Cache(name="L1",
           sets=64, ways=4, cl_size=cacheline_size,  # 16kB
           replacement_policy="LRU",
           write_back=True, write_allocate=False,
           store_to=wcc, load_from=l2, victims_to=None,
           write_combining=True,
           swap_on_load=False)  # inclusive/exclusive does not matter in first-level
cs = CacheSimulator(first_level=l1,
                    main_memory=mem)

# =========================
# POWER8 Cache Architecture
# =========================
cacheline_size = 128

mem = MainMemory()

# TODO + 128MB L4 (probably inclusive, LRU, 128B cl)

l3 = Cache(name="L3",
           sets=98304, ways=8, cl_size=cacheline_size,  # 12(cores) * 8MB(per core) = 96MB
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=None, load_from=None, victims_to=None,
           swap_on_load=False)
mem.store_from(l3)

l2 = Cache(name="L2",
           sets=512, ways=8, cl_size=cacheline_size,  # 512kB
           replacement_policy="LRU",
           write_back=True, write_allocate=True,
           store_to=l3, load_from=None, victims_to=l3,
           swap_on_load=False)
mem.load_to(l2)

l1 = Cache(name="L1",
           sets=64, ways=8, cl_size=cacheline_size,  # 64kB
           replacement_policy="LRU",
           write_back=False, write_allocate=False,
           store_to=l2, load_from=l2, victims_to=None,
           swap_on_load=False)  # inclusive/exclusive does not matter in first-level

# TODO write-combine buffers with 64B blocks and 32 sets

cs = CacheSimulator(first_level=l1,
                    main_memory=mem)
