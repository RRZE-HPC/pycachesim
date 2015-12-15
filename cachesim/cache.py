#!/usr/bin/env python
'''
Memory Hierarchy Simulator
'''
from __future__ import print_function
from __future__ import division
from __future__ import unicode_literals

import sys
import math
from copy import deepcopy
from collections import defaultdict
from functools import reduce
import operator

from cachesim import backend

if sys.version_info[0] < 3:
    range = xrange


def is_power2(num):
    return num > 0 and (num & (num - 1)) == 0


class CacheSimulator(object):
    '''High-level interface to the Cache Simulator.
    
    This is the only class that needs to be directly interfaced to.
    '''
    def __init__(self, first_level):
        '''
        Creates the interface that we interact with.
        
        :param first_level: first memory level object.
        '''
        assert isinstance(first_level, Cache), \
            "first_level needs to be a Cache object."
        
        self.first_level = first_level
        for l in self.levels():  # iterating to last level
            self.main_memory = l
        
        self.warmup_mode = False
    
    def enable_stats(self):
        '''Resets and enables statistics in all cache levels.
        
        Use this after warming up the caches to get a steady state result.
        '''
        for c in self.levels():
            c.enable_stats()
            
    def disable_stats(self):
        '''Disables statistics in all cache levels.
        
        Use this before warming up to increase speed.
        '''
        for c in self.levels():
            c.disable_stats()

    def load(self, addr, last_addr=None, length=None):
        if last_addr is not None:
            for a in range(addr, last_addr):
                self.first_level.load(a)
        elif length is not None:
            for a in range(addr, addr+length):
                self.first_level.load(a)
        else:
            self.first_level.load(addr)
    
    def store(self, addr, length=1, non_temporal=False):
        if non_temporal:
            raise ValueError("non_temporal stores are not yet supported")
        else:
            for a in range(addr, addr+length):
                self.first_level.store(a)

    def stats(self):
        '''Collects all stats from all cache levels.'''
        for c in self.levels():
            yield c.stats

    def levels(self):
        p = self.first_level
        while p is not None:
            yield p
            p = p.parent
    
    def __repr__(self):
        return 'CacheSimulator({!r})'.format(self.first_level)


class Cache(object):
    strategy_enum = {"FIFO": 0, "LRU": 1, "MRU": 2, "RR": 3}
    
    def __init__(self, sets, ways, cl_size, strategy="LRU", parent=None, level=None):
        '''Creates one cache level out of given configuration.
    
        :param sets: total number of sets, if 1 cache will be full-associative
        :param ways: total number of ways, if 1 cache will be direct mapped
        :param cl_size: number of bytes that can be addressed individually
        :param strategy: replacement strategy: FIFO, LRU (default), MRU or RR
        :param parent: the cache where misses are forwarded to, if None it is a last level cache
        
        The total cache size is the product of sets*ways*cl_size.
        Internally all addresses are converted to cacheline indices.
        
        Instantization has to happen from last level cache to first level cache, since each
        subsequent level requires a reference of the other level.
        '''
        assert parent is None or isinstance(parent, Cache), \
            "parent needs to be None or a Cache object."
        assert is_power2(cl_size), \
            "cl_size needs to be a power of two."
        assert parent is None or parent.cl_size <= cl_size, \
            "cl_size may only increase towards main memory."
        assert is_power2(ways), "ways needs to be a power of 2"
        assert strategy in self.strategy_enum, \
            "Unsupported strategy, we only support: "+', '.join(self.strategy_enum)
        
        self.strategy = strategy
        self.strategy_id = self.strategy_enum[strategy]
        self.parent = parent
        
        if parent is not None:
            self.backend = backend.Cache(
                sets, ways, cl_size, self.strategy_id, parent.backend)
        else:
            self.backend = backend.Cache(sets, ways, cl_size, self.strategy_id)
    
    def reset_stats(self):
        self.backend.HIT = 0
        self.backend.MISS = 0
        self.backend.LOAD = 0
        self.backend.STORE = 0
    
    def __getattr__(self, key):
        return getattr(self.backend, key)
    
    @property
    def stats(self):
        return {'LOAD': self.backend.LOAD,
                'STORE': self.backend.STORE,
                'HIT': self.backend.HIT,
                'MISS': self.backend.MISS}
    
    def load(self, addr):
        '''Load one element into the cache.
        '''
        self.backend.load(addr)

    def store(self, addr):
        # TODO (in c and here)
        pass
    
    def size(self):
        return self.sets*self.ways*self.cl_size
    
    def __repr__(self):
        return 'Cache(sets={!r}, ways={!r}, cl_size={!r}, strategy={!r}, parent={!r})'.format(
            self.sets, self.ways, self.cl_size, self.strategy, self.parent)


if __name__ == '__main__':
    l3 = Cache(20480, 16, 64, LRUPolicy())
    l2 = Cache(512, 8, 64, LRUPolicy(), parent=l3)
    l1 = Cache(64, 8, 64, LRUPolicy(), parent=l2)
    mh = CacheSimulator(l1)
    
    #mh.disable_stats()
    #mh.load(0, 50*1024*1024//4) # 50MB of doubles
    #mh.load(0, 1024*1024*1024//4) # 1GB
    mh.load(0, 1024)
    print(list(mh.stats()))