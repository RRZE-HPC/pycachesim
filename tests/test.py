'''
Unit tests for cachesim module
'''
from __future__ import print_function

import unittest
from pprint import pprint

from cachesim import CacheSimulator, Cache, MainMemory

# TODO Required Testcases:
# * write-through
# * weird trees with store_to and load _from
# * victim caches (not yet implemented)


class TestHighlevel(unittest.TestCase):
    def test_fill_nocl(self):
        l3 = Cache("L3", 4, 8, 1, "LRU", "write-back write-allocate")
        l2 = Cache("L2", 4, 4, 1, "LRU", "write-back write-allocate", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 1, "LRU", "write-back write-allocate", store_to=l2, load_from=l2)
        mem = MainMemory(l3)
        mh = CacheSimulator(l1, mem)
    
        mh.load(range(0, 32))
        mh.load(range(16,48))
        
        self.assertEqual(l1.cached, set(range(40,48)))
        self.assertEqual(l2.cached, set(range(32,48)))
        self.assertEqual(l3.cached, set(range(16,48)))

    def test_fill(self):
        l3 = Cache("L3", 4, 8, 8, "LRU", "write-back write-allocate")
        l2 = Cache("L2", 4, 4, 8, "LRU", "write-back write-allocate", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", "write-back write-allocate", store_to=l2, load_from=l2)
        mem = MainMemory(l3)
        mh = CacheSimulator(l1, mem)
    
        mh.load(range(0, 512))
        mh.load(range(448, 576))
        
        self.assertEqual(l1.cached, set(range(512, 576)))
        self.assertEqual(l2.cached, set(range(448, 576)))
        self.assertEqual(l3.cached, set(range(320, 576)))
    
    def _get_SandyEP_caches(self):
        # Cache hierarchy as found in a Sandy Brige EP:
        cacheline_size = 64
        l3 = Cache("L3", 20480, 16, cacheline_size,
                   "LRU", "write-back write-allocate")  # 20MB 16-ways
        l2 = Cache("L2", 512, 8, cacheline_size,
                   "LRU", "write-back write-allocate",
                   store_to=l3, load_from=l3)  # 256kB 8-ways
        l1 = Cache("L1", 64, 8, cacheline_size,
                   "LRU", "write-back write-allocate",
                   store_to=l2, load_from=l2)  # 32kB 8-ways
        mem = MainMemory(l3)
        mh = CacheSimulator(l1, mem)
        return mh, l1, l2, l3, mem, cacheline_size 
    
    def test_large_fill(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        mh.load(range(0, 32*1024))
        mh.reset_stats()
        mh.load(range(0, 32*1024))
        self.assertEqual(l1.LOAD, 32*1024)
        self.assertEqual(l2.LOAD, 0)
        self.assertEqual(l3.LOAD, 0)
        self.assertEqual(l1.HIT, 32*1024)
        self.assertEqual(l2.HIT, 0)
        self.assertEqual(l3.HIT, 0)
        self.assertEqual(l1.MISS, 0)
        self.assertEqual(l2.MISS, 0)
        self.assertEqual(l3.MISS, 0)
        self.assertEqual(l1.STORE, 0)
        self.assertEqual(l2.STORE, 0)
        self.assertEqual(l3.STORE, 0)
        self.assertEqual(l2.LOAD, 0)
        self.assertEqual(l3.LOAD, 0)
        
        mh.reset_stats()
        mh.load(range(0, 256*1024))
        self.assertEqual(l1.LOAD, 256*1024)
        self.assertEqual(l1.HIT, 32*1024+63*(256-32)*1024//64)
        self.assertEqual(l1.MISS, (256-32)*1024//64)
        self.assertEqual(l2.LOAD, (256-32)*1024//64)
        
        mh.load(range(0, 20*1024*1024))
        mh.reset_stats()
        mh.load(range(0, 20*1024*1024))
        self.assertEqual(l1.HIT+l2.HIT+l3.HIT, 20*1024*1024)
        self.assertEqual(l3.MISS, 0)

    # def test_large_store(self):
    #     # Cache hierarchy as found in a Sandy Brige EP:
    #     l3 = Cache(20480, 16, 64, "LRU")  # 20MB 16-ways
    #     l2 = Cache(512, 8, 64, "LRU", parent=l3)  # 256kB 8-ways
    #     l1 = Cache(64, 8, 64, "LRU", parent=l2)  # 32kB 8-ways
    #     mh = CacheSimulator(l1, write_allocate=False)
    #     mh.store(range(0, 20*1024*1024))
    #
    #     self.assertEqual(l1.LOAD, 0)
    #     self.assertEqual(l2.LOAD, 0)
    #     self.assertEqual(l3.LOAD, 0)
    #     self.assertEqual(l1.STORE, 20*1024*1024)
    #     self.assertEqual(l2.STORE, 20*1024*1024)
    #     self.assertEqual(l3.STORE, 20*1024*1024)

    def test_large_store_write_allocate(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()

        mh.store(range(0, 20*1024*1024))
        
        mh.force_write_back()
        
        self.assertEqual(l1.LOAD, 20*1024*1024)
        self.assertEqual(l2.LOAD, 20*1024*1024//64)
        self.assertEqual(l3.LOAD, 20*1024*1024//64)
        self.assertEqual(l1.STORE, 20*1024*1024)
        self.assertEqual(l2.STORE, 20*1024*1024//64)
        self.assertEqual(l3.STORE, 20*1024*1024//64)
    
    def test_large_fill_iter(self):
        mh, l1, l2, l3, mem, cacheline_size = self._get_SandyEP_caches()
        
        mh.load(range(0, 32*1024))
        mh.reset_stats()
        mh.load(range(0, 32*1024))
        self.assertEqual(l1.LOAD, 32*1024)
        self.assertEqual(l2.LOAD, 0)
        self.assertEqual(l3.LOAD, 0)
        self.assertEqual(mem.LOAD, 0)
        self.assertEqual(l1.HIT, 32*1024)
        self.assertEqual(l2.HIT, 0)
        self.assertEqual(l3.HIT, 0)
        self.assertEqual(mem.HIT, 0)
        self.assertEqual(l1.MISS, 0)
        self.assertEqual(l2.MISS, 0)
        self.assertEqual(l3.MISS, 0)
        self.assertEqual(mem.MISS, 0)
        self.assertEqual(l1.STORE, 0)
        self.assertEqual(l2.STORE, 0)
        self.assertEqual(l3.STORE, 0)
        self.assertEqual(mem.STORE, 0)
        
        mh.reset_stats()
        mh.load(range(0, 256*1024))
        self.assertEqual(l1.LOAD, 256*1024)
        self.assertEqual(l1.HIT, 32*1024+63*(256-32)*1024//64)
        self.assertEqual(l1.MISS, (256-32)*1024//64)
        self.assertEqual(l2.LOAD, (256-32)*1024//64)
        
        mh.load(range(0, 20*1024*1024))
        mh.reset_stats()
        mh.load(range(0, 20*1024*1024))
        self.assertEqual(l1.HIT+l2.HIT+l3.HIT, 20*1024*1024)
        self.assertEqual(l3.MISS, 0)
        self.assertEqual(mem.HIT, 0)
        self.assertEqual(mem.MISS, 0)
    
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
                
        self.assertEqual(l1.LOAD, cacheline_size*5+cacheline_size)
        self.assertEqual(l1.HIT, cacheline_size*(5+1)-2)
        self.assertEqual(l1.MISS, 2)
        self.assertEqual(l1.STORE, cacheline_size)
        
        self.assertEqual(l2.LOAD, 2)
        self.assertEqual(l2.HIT, 0)
        self.assertEqual(l2.MISS, 2)
        self.assertEqual(l2.STORE, 1)
        
        self.assertEqual(l3.LOAD, 2)
        self.assertEqual(l3.HIT, 0)
        self.assertEqual(l3.MISS, 2)
        self.assertEqual(l3.STORE, 1)
        
        self.assertEqual(mem.LOAD, 2)
        self.assertEqual(mem.HIT, 2)
        self.assertEqual(mem.MISS, 0)
        self.assertEqual(mem.STORE, 1)

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
        
        self.assertEqual(l1.LOAD, cacheline_size*(5+1))
        self.assertEqual(l1.HIT, cacheline_size*(5+1)-4)
        self.assertEqual(l1.MISS, 4)
        self.assertEqual(l1.STORE, cacheline_size)
        
        self.assertEqual(l2.LOAD, 4)
        self.assertEqual(l2.HIT, 2)
        self.assertEqual(l2.MISS, 2)
        self.assertEqual(l2.STORE, 1)
        
        self.assertEqual(l3.LOAD, 2)
        self.assertEqual(l3.HIT, 0)
        self.assertEqual(l3.MISS, 2)
        self.assertEqual(l3.STORE, 1)
        
        self.assertEqual(mem.LOAD, 2)
        self.assertEqual(mem.HIT, 2)
        self.assertEqual(mem.MISS, 0)
        self.assertEqual(mem.STORE, 1)

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
        mh.loadstore(offsets, length=element_size)
        
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
        
        self.assertEqual(l1.LOAD, cacheline_size*(5+1))
        self.assertEqual(l1.HIT, cacheline_size*(5+1)-4)
        self.assertEqual(l1.MISS, 4)
        self.assertEqual(l1.STORE, cacheline_size)
        
        self.assertEqual(l2.LOAD, 4)
        self.assertEqual(l2.HIT, 2)
        self.assertEqual(l2.MISS, 2)
        self.assertEqual(l2.STORE, 1)
        
        self.assertEqual(l3.LOAD, 2)
        self.assertEqual(l3.HIT, 0)
        self.assertEqual(l3.MISS, 2)
        self.assertEqual(l3.STORE, 1)
        
        self.assertEqual(mem.LOAD, 2)
        self.assertEqual(mem.HIT, 2)
        self.assertEqual(mem.MISS, 0)
        self.assertEqual(mem.STORE, 1)
        