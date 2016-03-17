'''
Unit tests for cachesim module
'''
from __future__ import print_function

import unittest
from pprint import pprint

from cachesim import CacheSimulator, Cache, MainMemory

# TODO Required Testcases:
# * write-through
# * weird trees with store_to and load_from
# * victim caches (not yet implemented)


class TestHighlevel(unittest.TestCase):
    def test_fill_nocl(self):
        mem = MainMemory()
        l3 = Cache("L3", 4, 8, 1, "LRU")
        mem.load_to(l3)
        mem.store_from(l3)
        l2 = Cache("L2", 4, 4, 1, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 1, "LRU", store_to=l2, load_from=l2)
        mh = CacheSimulator(l1, mem)
    
        mh.load(range(0, 32))
        mh.load(range(16,48))
        
        self.assertEqual(l1.cached, set(range(40,48)))
        self.assertEqual(l2.cached, set(range(32,48)))
        self.assertEqual(l3.cached, set(range(16,48)))

    def test_fill(self):
        mem = MainMemory()
        l3 = Cache("L3", 4, 8, 8, "LRU")
        mem.load_to(l3)
        mem.store_from(l3)
        l2 = Cache("L2", 4, 4, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", store_to=l2, load_from=l2)
        mh = CacheSimulator(l1, mem)
    
        mh.load(range(0, 512))
        mh.load(range(448, 576))
        
        self.assertEqual(l1.cached, set(range(512, 576)))
        self.assertEqual(l2.cached, set(range(448, 576)))
        self.assertEqual(l3.cached, set(range(320, 576)))

    def _get_SandyEP_caches(self):
        # Cache hierarchy as found in a Sandy Brige EP:
        cacheline_size = 64
        mem = MainMemory()
        l3 = Cache("L3", 20480, 16, cacheline_size,
                   "LRU",
                   write_back=True, write_allocate=True)  # 20MB 16-ways
        mem.load_to(l3)
        mem.store_from(l3)
        l2 = Cache("L2", 512, 8, cacheline_size,
                   "LRU",
                   write_back=True, write_allocate=True,
                   store_to=l3, load_from=l3)  # 256kB 8-ways
        l1 = Cache("L1", 64, 8, cacheline_size,
                   "LRU",
                   write_back=True, write_allocate=True,
                   store_to=l2, load_from=l2)  # 32kB 8-ways
        mh = CacheSimulator(l1, mem)
        return mh, l1, l2, l3, mem, cacheline_size 
    
    def test_large_fill(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        mh.load(range(0, 32*1024))
        mh.reset_stats()
        mh.load(range(0, 32*1024))
        self.assertEqual(l1.LOAD_count, 32*1024)
        self.assertEqual(l1.LOAD_byte, 32*1024)
        self.assertEqual(l2.LOAD_count, 0)
        self.assertEqual(l3.LOAD_count, 0)
        self.assertEqual(l1.HIT_count, 32*1024)
        self.assertEqual(l1.HIT_byte, 32*1024)
        self.assertEqual(l2.HIT_count, 0)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(l1.MISS_count, 0)
        self.assertEqual(l2.MISS_count, 0)
        self.assertEqual(l3.MISS_count, 0)
        self.assertEqual(l1.STORE_count, 0)
        self.assertEqual(l2.STORE_count, 0)
        self.assertEqual(l3.STORE_count, 0)
        self.assertEqual(l2.LOAD_count, 0)
        self.assertEqual(l3.LOAD_count, 0)

    def test_large_continious_store_write_allocate(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        length = 20*1024*1024
        mh.store(0,length)
        
        mh.force_write_back()
        
        self.assertEqual(l1.LOAD_count, length//64)
        self.assertEqual(l2.LOAD_count, length//64)
        self.assertEqual(l3.LOAD_count, length//64)
        self.assertEqual(l1.STORE_count, 1)
        self.assertEqual(l2.STORE_count, length//64)
        self.assertEqual(l3.STORE_count, length//64)

    def test_large_store_write_allocate(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        length = 20*1024*1024
        mh.store(range(0,length))
        
        mh.force_write_back()
        
        self.assertEqual(l1.LOAD_count, length//64)
        self.assertEqual(l2.LOAD_count, length//64)
        self.assertEqual(l3.LOAD_count, length//64)
        self.assertEqual(l1.STORE_count, length)
        self.assertEqual(l2.STORE_count, length//64)
        self.assertEqual(l3.STORE_count, length//64)

    def test_large_fill_iter(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        mh.load(range(0, 32*1024))
        mh.reset_stats()
        mh.load(range(0, 32*1024))
        self.assertEqual(l1.LOAD_count, 32*1024)
        self.assertEqual(l2.LOAD_count, 0)
        self.assertEqual(l3.LOAD_count, 0)
        self.assertEqual(mem.LOAD_count, 0)
        self.assertEqual(l1.HIT_count, 32*1024)
        self.assertEqual(l2.HIT_count, 0)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(mem.HIT_count, 0)
        self.assertEqual(l1.MISS_count, 0)
        self.assertEqual(l2.MISS_count, 0)
        self.assertEqual(l3.MISS_count, 0)
        self.assertEqual(mem.MISS_count, 0)
        self.assertEqual(l1.STORE_count, 0)
        self.assertEqual(l2.STORE_count, 0)
        self.assertEqual(l3.STORE_count, 0)
        self.assertEqual(mem.STORE_count, 0)
        
        mh.reset_stats()
        mh.load(range(0, 256*1024))
        self.assertEqual(l1.LOAD_count, 256*1024)
        self.assertEqual(l1.HIT_count, 32*1024+63*(256-32)*1024//64)
        self.assertEqual(l1.MISS_count, (256-32)*1024//64)
        self.assertEqual(l2.LOAD_count, (256-32)*1024//64)
        
        mh.load(range(0, 20*1024*1024))
        mh.reset_stats()
        mh.load(range(0, 20*1024*1024))
        self.assertEqual(l1.HIT_count+l2.HIT_count+l3.HIT_count, 20*1024*1024)
        self.assertEqual(l3.MISS_count, 0)
        self.assertEqual(mem.HIT_count, 0)
        self.assertEqual(mem.MISS_count, 0)
    
    def _build_2d5pt_offset(self, i, j, matrix_width, matrix_height, element_size=8):
        return (# Loads:
                [((j-1)*matrix_width+i)*element_size,
                 (j*matrix_width+i-1)*element_size,
                 (j*matrix_width+i)*element_size,
                 (j*matrix_width+i+1)*element_size,
                 ((j+1)*matrix_width+i)*element_size], 
                # Stores:
                [(matrix_width*matrix_height + matrix_width*j+i)*element_size])

    def test_2d5pt_L1_layerconditions(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        element_size = 8  # 8-byte doubles
        
        # Layer condition in L1:
        elements_per_cl = cacheline_size//element_size
        elements_in_cache = l1.size()//element_size
        kernel_height = 3
        matrix_width = elements_in_cache // kernel_height // 2 #Bad: +19#+18#+14#+11#+10#+6#+4#+3#+2 # TODO check other sizes
        matrix_height = 100 # needs to be large enough for evictions of stores to happen
        
        # Warm up:
        # Go through half the matrix:
        for j in range(1, matrix_height//2+1):
            if j < matrix_height//2:
                # iterate full rows for half the matrix
                warmup_imax = matrix_width-1
            else:
                # and(!) another third-row (to avoid discontinuities at boundary):
                warmup_imax = matrix_width//3
            for i in range(1, warmup_imax):
                loff, soff = self._build_2d5pt_offset(
                    i, j, matrix_width, matrix_height, element_size)
                mh.load(loff, length=element_size)
                mh.store(soff, length=element_size)

        
        j = matrix_height//2
        warmup_imax = matrix_width//3

        mh.reset_stats()
        
        # Benchmark:
        for i in range(warmup_imax, warmup_imax+elements_per_cl):
            loff, soff = self._build_2d5pt_offset(
                i, j, matrix_width, matrix_height, element_size)
            mh.load(loff, length=element_size)
            mh.store(soff, length=element_size)
                
        # L1 LOAD: five loads and one write-allocate 
        # (only one, because afterwards all elements are present in cache)
        self.assertEqual(l1.LOAD_count, elements_per_cl*5 + 1)
        # L1 LOAD (bytes): five cachelines from loads and one cacheline from write-allocate/store
        self.assertEqual(l1.LOAD_byte, 5*cacheline_size + 1*cacheline_size)
        # L1 HIT: five x elements_per_cl from loads, minus one from leading access
        self.assertEqual(l1.HIT_count, elements_per_cl*5 - 1)
        self.assertEqual(l1.HIT_byte, cacheline_size*5 - element_size)
        self.assertEqual(l1.MISS_count, 2)
        # L1 MISS (bytes): leading element from load and full cachline from store
        self.assertEqual(l1.MISS_byte, 1*element_size + 1*cacheline_size)
        self.assertEqual(l1.STORE_count, elements_per_cl)
        self.assertEqual(l1.STORE_byte, cacheline_size)
        
        self.assertEqual(l2.LOAD_count, 2)
        self.assertEqual(l2.HIT_count, 0)
        self.assertEqual(l2.MISS_count, 2)
        self.assertEqual(l2.STORE_count, 1)
        
        self.assertEqual(l3.LOAD_count, 2)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(l3.MISS_count, 2)
        self.assertEqual(l3.STORE_count, 1)
        
        self.assertEqual(mem.LOAD_count, 2)
        self.assertEqual(mem.HIT_count, 2)
        self.assertEqual(mem.MISS_count, 0)
        self.assertEqual(mem.STORE_count, 1)

    def test_2d5pt_L2_layerconditions(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        element_size = 8  # 8-byte doubles
        
        # Layer condition in L2:
        elements_per_cl = cacheline_size//element_size
        elements_in_cache = l2.size()//element_size
        kernel_height = 3
        matrix_width = elements_in_cache // kernel_height // 2  # TODO check other sizes
        matrix_height = 20 # needs to be large enough for evictions of stores to happen
        
        # Warm up:
        for j in range(1, matrix_height//2+1):
            if j < matrix_height//2:
                # iterate full rows for half the matrix
                warmup_imax = matrix_width-1
            else:
                # and(!) another third-row (to avoid discontinuities at boundary):
                warmup_imax = matrix_width//3
            for i in range(1, warmup_imax):
                loff, soff = self._build_2d5pt_offset(
                    i, j, matrix_width, matrix_height, element_size)
                mh.load(loff, length=element_size)
                mh.store(soff, length=element_size)
        
        j = matrix_height//2
        warmup_imax = matrix_width//3
        
        mh.reset_stats()
        
        # Benchmark:
        for i in range(warmup_imax, warmup_imax+elements_per_cl):
            loff, soff = self._build_2d5pt_offset(
                i, j, matrix_width, matrix_height, element_size)
            mh.load(loff, length=element_size)
            mh.store(soff, length=element_size)
        
        self.assertEqual(l1.LOAD_count, elements_per_cl*5 + 1)
        self.assertEqual(l1.LOAD_byte, cacheline_size*5 + cacheline_size)
        # 3 uncached leading accesses with 2D LC in L1
        self.assertEqual(l1.HIT_count, elements_per_cl*5 - 3)
        self.assertEqual(l1.HIT_byte, cacheline_size*5 - 3*element_size)
        self.assertEqual(l1.MISS_count, 4)
        self.assertEqual(l1.MISS_byte, 3*element_size + cacheline_size)
        self.assertEqual(l1.STORE_count, elements_per_cl)
        self.assertEqual(l1.STORE_byte, cacheline_size)
        
        self.assertEqual(l2.LOAD_count, 4)
        self.assertEqual(l2.LOAD_byte, 4*cacheline_size)
        self.assertEqual(l2.HIT_count, 2)
        self.assertEqual(l2.HIT_byte, 2*cacheline_size)
        self.assertEqual(l2.MISS_count, 2)
        self.assertEqual(l2.MISS_byte, 2*cacheline_size)
        self.assertEqual(l2.STORE_count, 1)
        self.assertEqual(l2.STORE_byte, cacheline_size)
        
        self.assertEqual(l3.LOAD_count, 2)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(l3.MISS_count, 2)
        self.assertEqual(l3.STORE_count, 1)
        
        self.assertEqual(mem.LOAD_count, 2)
        self.assertEqual(mem.HIT_count, 2)
        self.assertEqual(mem.MISS_count, 0)
        self.assertEqual(mem.STORE_count, 1)

    def test_2d5pt_L2_layerconditions_loadstore(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        element_size = 8  # 8-byte doubles
        
        # Layer condition in L1:
        elements_per_cl = cacheline_size//element_size
        elements_in_cache = l2.size()//element_size
        kernel_height = 3
        matrix_width = elements_in_cache // kernel_height // 2  # TODO check other sizes
        matrix_height = 20 # needs to be large enough for evictions of stores to happen
        
        # Warm up:
        offsets = []
        for j in range(1, matrix_height//2+1):
            if j < matrix_height//2:
                # iterate full rows for half the matrix
                warmup_imax = matrix_width-1
            else:
                # and(!) another third-row (to avoid discontinuities at boundary):
                warmup_imax = matrix_width//3
            for i in range(1, warmup_imax):
                offsets.append(self._build_2d5pt_offset(
                    i, j, matrix_width, matrix_height, element_size))
        
        # Execute warmup
        mh.loadstore(offsets, length=element_size)
        
        j = matrix_height//2
        warmup_imax = matrix_width//3
        
        mh.reset_stats()
        
        # Benchmark:
        offsets = []
        for i in range(warmup_imax, warmup_imax+elements_per_cl):
            offsets.append(self._build_2d5pt_offset(
                i, j, matrix_width, matrix_height, element_size))
        mh.loadstore(offsets, length=element_size)
        
        self.assertEqual(l1.LOAD_count, elements_per_cl*5+1)
        self.assertEqual(l1.LOAD_byte, cacheline_size*5 + cacheline_size)
        self.assertEqual(l1.HIT_count, elements_per_cl*5 - 3)
        self.assertEqual(l1.HIT_byte, cacheline_size*5 - 3*element_size)
        self.assertEqual(l1.MISS_count, 4)
        self.assertEqual(l1.MISS_byte, 3*element_size + cacheline_size)
        self.assertEqual(l1.STORE_count, elements_per_cl)
        self.assertEqual(l1.STORE_byte, cacheline_size)
        
        self.assertEqual(l2.LOAD_count, 4)
        self.assertEqual(l2.HIT_count, 2)
        self.assertEqual(l2.MISS_count, 2)
        self.assertEqual(l2.STORE_count, 1)
        
        self.assertEqual(l3.LOAD_count, 2)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(l3.MISS_count, 2)
        self.assertEqual(l3.STORE_count, 1)
        
        self.assertEqual(mem.LOAD_count, 2)
        self.assertEqual(mem.HIT_count, 2)
        self.assertEqual(mem.MISS_count, 0)
        self.assertEqual(mem.STORE_count, 1)
    
    def _build_Bulldozer_caches(self):
        cacheline_size = 64
        
        mem = MainMemory(name="MEM")
        l3 = Cache(name="L3",
                   sets=2048, ways=64, cl_size=cacheline_size,  # 4MB
                   replacement_policy="LRU",
                   write_back=True, write_allocate=False,  # victim caches don't need write-allocate
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
                   replacement_policy="LRU",
                   write_combining=True, subblock_size=1,
                   write_back=True, write_allocate=False,  # this policy only makes sens with WCC
                   store_to=l2, load_from=None, victims_to=None,
                   swap_on_load=False)
        l1 = Cache(name="L1",
                   sets=64, ways=4, cl_size=cacheline_size,  # 16kB
                   replacement_policy="LRU",
                   write_back=False, write_allocate=False,
                   store_to=wcc, load_from=l2, victims_to=None,
                   swap_on_load=False)  # inclusive/exclusive does not matter in first-level
        cs = CacheSimulator(first_level=l1,
                            main_memory=mem)
        
        return cs, l1, wcc, l2, l3, mem, cacheline_size

    def test_victim_write_back_cache(self):
        cs, l1, wcc, l2, l3, mem, cacheline_size = self._build_Bulldozer_caches()
        # STREAM copy 10MB in cacheline chunks
        iteration_size = 128//cacheline_size#1024*1024*10//cacheline_size
        offset = iteration_size*cacheline_size
        for i in range(0, iteration_size*cacheline_size, cacheline_size):
            cs.load(i, cacheline_size)
            cs.store(offset+i, cacheline_size)
        
        cs.force_write_back()
        
        self.assertEqual(l1.LOAD_count, iteration_size)
        self.assertEqual(l1.LOAD_byte, iteration_size*cacheline_size)
        self.assertEqual(l1.HIT_count, 0)
        self.assertEqual(l1.HIT_byte, 0)
        self.assertEqual(l1.MISS_count, iteration_size)
        self.assertEqual(l1.STORE_count, iteration_size)
        self.assertEqual(l1.STORE_byte, iteration_size*cacheline_size)
        
        self.assertEqual(wcc.LOAD_count, 0)
        self.assertEqual(wcc.HIT_count, 0)
        self.assertEqual(wcc.MISS_count, 0)
        self.assertEqual(wcc.STORE_count, iteration_size)
        
        # TODO why -1 ?
        self.assertEqual(l2.LOAD_count, iteration_size)
        self.assertEqual(l2.HIT_count, 0)
        self.assertEqual(l2.MISS_count, iteration_size)
        self.assertEqual(l2.STORE_count, iteration_size)
        
        self.assertEqual(l3.LOAD_count, iteration_size)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(l3.MISS_count, iteration_size)
        self.assertEqual(l3.STORE_count, iteration_size)
        
        self.assertEqual(mem.LOAD_count, iteration_size)
        self.assertEqual(mem.HIT_count, iteration_size)
        self.assertEqual(mem.MISS_count, 0)
        self.assertEqual(mem.STORE_count, iteration_size)

    def test_write_combining(self):
        cs, l1, wcc, l2, l3, mem, cacheline_size = self._build_Bulldozer_caches()
        # STREAM copy one cacheline in byte chunks
        for i in range(0, cacheline_size):
            cs.store(i)
        
        cs.force_write_back()
        
        # write-combining should eliminate the write-allocate in L2
        self.assertEqual(l1.LOAD_count, 0)
        self.assertEqual(l1.LOAD_byte, 0)
        self.assertEqual(l1.HIT_count, 0)
        self.assertEqual(l1.HIT_byte, 0)
        self.assertEqual(l1.MISS_count, 0)
        self.assertEqual(l1.STORE_count, cacheline_size)
        self.assertEqual(l1.STORE_byte, cacheline_size)
        
        self.assertEqual(wcc.LOAD_count, 0)
        self.assertEqual(wcc.HIT_count, 0)
        self.assertEqual(wcc.MISS_count, 0)
        self.assertEqual(wcc.STORE_count, cacheline_size)
        
        self.assertEqual(l2.LOAD_count, 0)
        self.assertEqual(l2.HIT_count, 0)
        self.assertEqual(l2.MISS_count, 0)
        self.assertEqual(l2.STORE_count, 1)
        self.assertEqual(l2.STORE_byte, cacheline_size)
        
        self.assertEqual(l3.LOAD_count, 0)
        self.assertEqual(l3.HIT_count, 0)
        self.assertEqual(l3.MISS_count, 0)
        self.assertEqual(l3.STORE_count, 1)
        
        self.assertEqual(mem.LOAD_count, 0)
        self.assertEqual(mem.HIT_count, 0)
        self.assertEqual(mem.MISS_count, 0)
        self.assertEqual(mem.STORE_count, 1)
        
        