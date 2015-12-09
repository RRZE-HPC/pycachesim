'''
Unit tests for cachesim module
'''
import sys
import unittest
sys.path[0:0] = ['.', '..']

from cachesim import MemoryHierarchy, Cache, LRUPolicy

class TestHighlevel(unittest.TestCase):
    def test_fill_nocl(self):
        l3 = Cache(4, 8, 1, LRUPolicy())
        l2 = Cache(4, 4, 1, LRUPolicy(), parent=l3)
        l1 = Cache(2, 4, 1, LRUPolicy(), parent=l2)
        mh = MemoryHierarchy(l1)
    
        mh.load(0, 32)
        mh.load(16,48)
        
        self.assertItemsEqual(l1.cached, range(40,48))
        self.assertItemsEqual(l2.cached, range(32,48))
        self.assertItemsEqual(l3.cached, range(16,48))

    def test_fill(self):
        l3 = Cache(4, 8, 8, LRUPolicy())
        l2 = Cache(4, 4, 8, LRUPolicy(), parent=l3)
        l1 = Cache(2, 4, 8, LRUPolicy(), parent=l2)
        mh = MemoryHierarchy(l1)
    
        mh.load(0, 512)
        mh.load(448, 576)
        
        self.assertItemsEqual(l1.cached, range(512, 576))
        self.assertItemsEqual(l2.cached, range(448, 576))
        self.assertItemsEqual(l3.cached, range(320, 576))     