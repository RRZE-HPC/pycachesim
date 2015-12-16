'''
Unit tests for cachesim module
'''
from __future__ import print_function

import unittest

from cachesim import CacheSimulator, Cache

class TestHighlevel(unittest.TestCase):
    def test_fill_nocl(self):
        l3 = Cache(4, 8, 1, "LRU")
        l2 = Cache(4, 4, 1, "LRU", parent=l3)
        l1 = Cache(2, 4, 1, "LRU", parent=l2)
        mh = CacheSimulator(l1)
    
        mh.load(0, 32)
        mh.load(16,48)
        
        self.assertEqual(l1.cached, set(range(40,48)))
        self.assertEqual(l2.cached, set(range(32,48)))
        self.assertEqual(l3.cached, set(range(16,48)))

    def test_fill(self):
        l3 = Cache(4, 8, 8,"LRU")
        l2 = Cache(4, 4, 8,"LRU", parent=l3)
        l1 = Cache(2, 4, 8,"LRU", parent=l2)
        mh = CacheSimulator(l1)
    
        mh.load(0, 512)
        mh.load(448, 576)
        
        self.assertEqual(l1.cached, set(range(512, 576)))
        self.assertEqual(l2.cached, set(range(448, 576)))
        self.assertEqual(l3.cached, set(range(320, 576)))     
    
    def test_large_fill(self):
        # Cache hierarchy as found in a Sandy Brige EP:
        l3 = Cache(20480, 16, 64, "LRU")  # 20MB 16-ways
        l2 = Cache(512, 8, 64, "LRU", parent=l3)  # 256kB 8-ways
        l1 = Cache(64, 8, 64, "LRU", parent=l2)  # 32kB 8-ways
        mh = CacheSimulator(l1)
        
        mh.load(0, 32*1024)
        mh.reset_stats()
        mh.load(0, 32*1024)
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
        mh.load(0, 256*1024)
        self.assertEqual(l1.LOAD, 256*1024)
        self.assertEqual(l1.HIT, 32*1024+63*(256-32)*1024//64)
        self.assertEqual(l1.MISS, (256-32)*1024//64)
        self.assertEqual(l2.LOAD, (256-32)*1024//64)
        
        mh.load(0, 20*1024*1024)
        mh.reset_stats()
        mh.load(0, 20*1024*1024)
        self.assertEqual(l1.HIT+l2.HIT+l3.HIT, 20*1024*1024)
        self.assertEqual(l3.MISS, 0)

    def test_large_store(self):
        # Cache hierarchy as found in a Sandy Brige EP:
        l3 = Cache(20480, 16, 64, "LRU")  # 20MB 16-ways
        l2 = Cache(512, 8, 64, "LRU", parent=l3)  # 256kB 8-ways
        l1 = Cache(64, 8, 64, "LRU", parent=l2)  # 32kB 8-ways
        mh = CacheSimulator(l1)
        mh.store(0, 20*1024*1024)
        
        self.assertEqual(l1.LOAD, 0)
        self.assertEqual(l2.LOAD, 0)
        self.assertEqual(l3.LOAD, 0)
        self.assertEqual(l1.STORE, 20*1024*1024)
        self.assertEqual(l2.STORE, 20*1024*1024)
        self.assertEqual(l3.STORE, 20*1024*1024)
        