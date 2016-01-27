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
    def __init__(self, first_level, main_memory, write_allocate=True):
        '''
        Creates the interface that we interact with.
        
        :param first_level: first cache level object.
        :param main_memory: main memory object.
        :param write_allocate: if True, will load a cacheline before store
        '''
        assert isinstance(first_level, Cache), \
            "first_level needs to be a Cache object."
        assert isinstance(main_memory, MainMemory), \
            "main_memory needs to be a MainMemory object"
        assert write_allocate, "Non write-allocate architectures are currently not supported."
        
        self.first_level = first_level
        for l in self.levels(with_mem=False):  # iterating to last level
            self.last_level = l
        
        assert main_memory.last_level == self.last_level, \
            "Main memory's last level reference needs to coincide with the last cache level."
        self.main_memory = main_memory
        
        self.write_allocate = write_allocate
    
    def reset_stats(self):
        '''Resets statistics in all cache levels.
        
        Use this after warming up the caches to get a steady state result.
        '''
        for c in self.levels():
            c.reset_stats()

    def load(self, addr, last_addr=None, length=1):
        '''Loads one or more addresses.
        
        if last_addr is not None, it all addresses between addr and last_addr (exclusive) are loaded
        if lengh is given, all address from addr until addr+length (exclusive) are loaded
        '''
        if addr is None:
            return
        elif not isinstance(addr, Iterable):
            self.first_level.load(addr, last_addr=last_addr, length=length)
        else:
            self.first_level.iterload(addr, length=length)
    
    def store(self, addr, last_addr=None, length=1, non_temporal=False):
        if non_temporal:
            raise ValueError("non_temporal stores are not yet supported")
        
        if self.write_allocate:
            self.load(addr, last_addr, length)
        
        if addr is None:
            return
        elif not isinstance(addr, Iterable):
            self.first_level.store(addr, last_addr=last_addr, length=length)
        else:
            self.first_level.iterstore(addr, length=length)

    def loadstore(self, addrs, length=1):
        '''Takes load and store address to be evaluated in order.
        
        :param addrs: iteratable of address tuples: [(loads, stores), ...]
        :param length: will load and store all bytes between addr and addr+length (for each address)
        '''
        
        if not isinstance(addrs, Iterable):
            raise ValueError("addr must be iteratable")
        
        self.first_level.loadstore(addrs, length=length, write_allocate=self.write_allocate)

    def stats(self):
        '''Collects all stats from all cache levels.'''
        for c in self.levels():
            yield c.stats

    def levels(self, with_mem=True):
        p = self.first_level
        while p is not None:
            yield p
            p = p.parent
        
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
    
    def __init__(self, sets, ways, cl_size, replacement_policy="LRU", parent=None):
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
        assert parent is None or isinstance(parent, Cache), \
            "parent needs to be None or a Cache object."
        assert is_power2(cl_size), \
            "cl_size needs to be a power of two."
        assert parent is None or parent.cl_size <= cl_size, \
            "cl_size may only increase towards main memory."
        assert is_power2(ways), "ways needs to be a power of 2"
        assert replacement_policy in self.replacement_policy_enum, \
            "Unsupported replacement strategy, we only support: "+ \
            ', '.join(self.replacement_policy_enum)
        
        self.replacement_policy = replacement_policy
        self.replacement_policy_id = self.replacement_policy_enum[replacement_policy]
        self.parent = parent
        
        if parent is not None:
            self.backend = backend.Cache(
                sets, ways, cl_size, self.replacement_policy_id, parent.backend)
        else:
            self.backend = backend.Cache(sets, ways, cl_size, self.replacement_policy_id)
    
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
    
    def load(self, addr, last_addr=None, length=1):
        '''Load elements into the cache.
        '''
        if last_addr is not None:
            self.backend.load(addr, length=last_addr-addr)
        else:
            self.backend.load(addr, length=length)

    def store(self, addr, last_addr=None, length=1):
        '''Stores elements via the cache.
        '''
        if last_addr is not None:
            self.backend.store(addr, length=last_addr-addr)
        else:
            self.backend.store(addr, length=length)
    
    def loadstore(self, addrs, length=1, write_allocate=True):
        '''Loads and stores combined in one iterator'''
        self.backend.loadstore(addrs, length, write_allocate)
    
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
        assert last_level.parent is None, \
            "last_level must be a last level cache (last_level.parent is None)."
        
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
