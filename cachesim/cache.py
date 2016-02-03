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
from collections import defaultdict, Iterable
from functools import reduce
import operator

# from PIL import Image

from cachesim import backend

if sys.version_info[0] < 3:
    range = xrange


def is_power2(num):
    return num > 0 and (num & (num - 1)) == 0


class CacheSimulator(object):
    '''High-level interface to the Cache Simulator.
    
    This is the only class that needs to be directly interfaced to.
    '''
    def __init__(self, first_level, main_memory):
        '''
        Creates the interface that we interact with.
        
        :param first_level: first cache level object.
        :param main_memory: main memory object.
        '''
        assert isinstance(first_level, Cache), \
            "first_level needs to be a Cache object."
        assert isinstance(main_memory, MainMemory), \
            "main_memory needs to be a MainMemory object"
        
        self.first_level = first_level
        for l in self.levels(with_mem=False):  # iterating to last level
            self.last_level = l
        
        assert main_memory.last_level == self.last_level, \
            "Main memory's last level reference needs to coincide with the last cache level."
        self.main_memory = main_memory
        
    def reset_stats(self):
        '''Resets statistics in all cache levels.
        
        Use this after warming up the caches to get a steady state result.
        '''
        for c in self.levels(with_mem=False):
            c.reset_stats()
    
    def force_write_back(self):
        '''Write all pending dirty lines back.'''
        # force_write_back() is acting recursive by it self, but multiple write-back first level 
        # caches are imaginable. Better safe then sorry:
        for c in self.levels(with_mem=False):
            c.force_write_back()

    def load(self, addr, length=1):
        '''Loads one or more addresses.
        
        if lengh is given, all address from addr until addr+length (exclusive) are loaded
        '''
        if addr is None:
            return
        elif not isinstance(addr, Iterable):
            self.first_level.load(addr, length=length)
        else:
            self.first_level.iterload(addr, length=length)
    
    def store(self, addr, length=1, non_temporal=False):
        if non_temporal:
            raise ValueError("non_temporal stores are not yet supported")
        
        if addr is None:
            return
        elif not isinstance(addr, Iterable):
            self.first_level.store(addr, length=length)
        else:
            self.first_level.iterstore(addr, length=length)

    def loadstore(self, addrs, length=1):
        '''Takes load and store address to be evaluated in order.
        
        :param addrs: iteratable of address tuples: [(loads, stores), ...]
        :param length: will load and store all bytes between addr and addr+length (for each address)
        '''
        
        if not isinstance(addrs, Iterable):
            raise ValueError("addr must be iteratable")
        
        self.first_level.loadstore(addrs, length=length)

    def stats(self):
        '''Collects all stats from all cache levels.'''
        for c in self.levels():
            yield c.stats

    def levels(self, with_mem=True):
        p = self.first_level
        while p is not None:
            yield p
            # FIXME victim caches will be victims of ignorance (unreachable via .load_from)
            p = p.load_from
        
        if with_mem:
            yield self.main_memory
    
    # def draw_array(self, start, width, height, block=1):
    #     length = (width*height)//block
    #     canvas = Image.new("RGB", (width, height)) # FIXME: switch to palette "P" with ImagePalette
    #
    #     for h in range(height):
    #         for w in range(width):
    #             addr = start+h*(width*block)+w*block
    #
    #             l1 = self.first_level
    #             l2 = self.first_level.parent
    #             l3 = self.first_level.parent.parent
    #             if l1.contains(addr):
    #                 canvas.putpixel((w,h), (0,0,255))
    #             elif l2.contains(addr):
    #                 canvas.putpixel((w,h), (255,0,0))
    #             elif l3.contains(addr):
    #                 canvas.putpixel((w,h), (0,255,0))
    #             else:
    #                 canvas.putpixel((w,h), (255,255,255))
    #
    #     return canvas

    def __repr__(self):
        return 'CacheSimulator({!r}, {!r})'.format(self.first_level, self.main_memory)


class Cache(object):
    replacement_policy_enum = {"FIFO": 0, "LRU": 1, "MRU": 2, "RR": 3}
    write_policy_enum = {"write-back write-allocate": 0, "write-through non-write-allocate": 1}
    
    def __init__(self, name, sets, ways, cl_size,
                 replacement_policy="LRU",
                 write_policy="write-through write-allocate",
                 load_from=None, store_to=None, victims_to=None,
                 swap_on_load=False):
        '''Creates one cache level out of given configuration.
    
        :param sets: total number of sets, if 1 cache will be full-associative
        :param ways: total number of ways, if 1 cache will be direct mapped
        :param cl_size: number of bytes that can be addressed individually
        :param replacement_policy: FIFO, LRU (default), MRU or RR
        :param parent: the cache where misses are forwarded to, if None it is a last level cache
        
        The total cache size is the product of sets*ways*cl_size.
        Internally all addresses are converted to cacheline indices.
        
        Instantization has to happen from last level cache to first level cache, since each
        subsequent level requires a reference of the other level.
        '''
        assert store_to is None or isinstance(store_to, Cache), \
            "store_to needs to be None or a Cache object."
        assert load_from is None or isinstance(load_from, Cache), \
            "load_from needs to be None or a Cache object."
        assert victims_to is None or isinstance(load_from, Cache), \
            "victims_to needs to be None or a Cache object."
        assert is_power2(cl_size), \
            "cl_size needs to be a power of two."
        assert store_to is None or store_to.cl_size <= cl_size, \
            "cl_size may only increase towards main memory."
        assert load_from is None or load_from.cl_size <= cl_size, \
            "cl_size may only increase towards main memory."
        assert is_power2(ways), "ways needs to be a power of 2"
        assert replacement_policy in self.replacement_policy_enum, \
            "Unsupported replacement strategy, we only support: "+ \
            ', '.join(self.replacement_policy_enum)
        assert write_policy in self.write_policy_enum, \
            "Unsupported write policy, we only support: "+ \
            ', '.join(self.write_policy_enum)
        # TODO check that ways only increase from higher  to lower _exclusive_ cache
        # other wise swap won't be a valid procedure to ensure exclusiveness
        
        self.name = name
        self.replacement_policy = replacement_policy
        self.replacement_policy_id = self.replacement_policy_enum[replacement_policy]
        self.write_policy = write_policy
        self.write_policy_id = self.write_policy_enum[write_policy]
        self.store_to = store_to
        self.load_from = load_from
        self.victims_to = victims_to
        self.swap_on_load = swap_on_load
        
        self.backend = backend.Cache(
            name, sets, ways, cl_size,
            self.replacement_policy_id, self.write_policy_id,
            self._get_backend(load_from), self._get_backend(store_to),
            self._get_backend(victims_to),
            swap_on_load)
    
    def _get_backend(self, cache):
        '''Returns backend of *cache* unless *cache* is None, then None is returned.'''
        if cache is not None:
            return cache.backend
        return None
    
    def get_cl_start(self, addr):
        '''Returns first address belonging to the same cacheline as *addr*'''
        return addr >> self.backend.cl_bits << self.backend.cl_bits
    
    def get_cl_end(self, addr):
        '''Returns last address belonging to the same cacheline as *addr*'''
        return self.get_cl_start(addr) + self.backend.cl_size - 1
    
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
    
    def size(self):
        return self.sets*self.ways*self.cl_size
    
    def __repr__(self):
        return 'Cache(sets={!r}, ways={!r}, cl_size={!r}, replacement_policy={!r}, parent={!r})'.format(
            self.sets, self.ways, self.cl_size, self.replacement_policy, self.parent)


class MainMemory(object):
    def __init__(self, last_level):
        '''Creates one cache level out of given configuration.
    
        :param last_level: last level cache
        
        '''
        assert isinstance(last_level, Cache), \
            "last_level needs to be a Cache object."
        assert last_level.load_from is None, \
            "last_level must be a last level cache (last_level.load_from is None)."
        
        self.last_level = last_level
    
    def reset_stats(self):
        # since all stats in main memory are derived from the last level cache, there is nothing to 
        # reset
        pass
    
    def __getattr__(self, key):
        try:
            return self.stats[key]
        except KeyError:
            raise AttributeError
    
    @property
    def stats(self):
        return {'LOAD': self.last_level.MISS,
                'STORE': self.last_level.STORE,
                'HIT': self.last_level.MISS,
                'MISS': 0}
    
    def __repr__(self):
        return 'MainMemory(last_level={!r})'.format(self.last_level)
