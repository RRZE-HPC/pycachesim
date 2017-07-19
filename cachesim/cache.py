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

        self.main_memory = main_memory

    @classmethod
    def from_dict(cls, d):
        '''Creates cache hierarchy from dictionary.'''
        main_memory = MainMemory()
        caches = {}
        first_level = None

        referred_caches = set()

        # First pass, create all named caches and collect references
        for name, conf in d.items():
            caches[name] = Cache(
                name=name, **{k:v for k,v in conf.items() if k not in ['store_to', 'load_from']})
            if 'store_to' in conf:
                referred_caches.add(conf['store_to'])
            if 'load_from' in conf:
                referred_caches.add(conf['load_from'])

        # Second pass, connect caches
        for name, conf in d.items():
            if 'store_to' in conf and conf['store_to'] is not None:
                caches[name].set_store_to(caches[conf['store_to']])
            if 'load_from' in conf and conf['load_from'] is not None:
                caches[name].set_load_from(caches[conf['load_from']])

        # Find first level (not target of any load_from or store_to)
        first_level = set(d.keys()) - referred_caches
        assert len(first_level) == 1, "Unable to find first cache level."
        first_level = caches[list(first_level)[0]]

        # Find last level (has no load_from or store_to target)
        last_level = [name for name, conf in d.items()
                      if ('store_to' not in conf or conf['store_to'] is None) and
                         ('load_from' not in conf or conf['load_from'] is None)]
        assert len(last_level) == 1, "Unable to find last cache level."
        last_level = caches[last_level[0]]
        # Set main memory connections
        # FIXME could be solved nicer by giving mem an entry in input dict
        main_memory.load_to(last_level)
        main_memory.store_from(last_level)

        return cls(first_level, main_memory), caches, main_memory

    def reset_stats(self):
        '''Resets statistics in all cache levels.

        Use this after warming up the caches to get a steady state result.
        '''
        for c in self.levels(with_mem=False):
            c.reset_stats()

    def force_write_back(self):
        '''Write all pending dirty lines back.'''
        # force_write_back() is acting recursive by it self, but multiple write-back first level
        # caches are imaginable. Better safe than sorry:
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
            yield c.stats()

    def print_stats(self, header=True, file=sys.stdout):
        '''Pretty print stats table'''
        if header:
            print("CACHE {:*^18} {:*^18} {:*^18} {:*^18} {:*^18}".format(
                      "HIT", "MISS", "LOAD", "STORE", "EVICT"),
                  file=file)
        for s in self.stats():
            print("{name:>5} {HIT_count:>6} ({HIT_byte:>8}B) {MISS_count:>6} ({MISS_byte:>8}B) "
                  "{LOAD_count:>6} ({LOAD_byte:>8}B) {STORE_count:>6} "
                  "({STORE_byte:>8}B) {EVICT_count:>6} ({EVICT_byte:>8}B)".format(
                      HIT_bytes=2342, **s),
                  file=file)

    def levels(self, with_mem=True):
        p = self.first_level
        while p is not None:
            yield p
            # FIXME bad hack to include victim caches, need a more general solution, probably
            # involving recursive tree walking
            if p.victims_to is not None and p.victims_to != p.load_from:
                yield p.victims_to
            if p.store_to is not None and p.store_to != p.load_from and p.store_to != p.victims_to:
                yield p.store_to
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

    def __init__(self, name, sets, ways, cl_size,
                 replacement_policy="LRU",
                 write_back=True,
                 write_allocate=True,
                 write_combining=False,
                 subblock_size=None,
                 load_from=None, store_to=None, victims_to=None,
                 swap_on_load=False):
        '''Creates one cache level out of given configuration.

        :param sets: total number of sets, if 1 cache will be full-associative
        :param ways: total number of ways, if 1 cache will be direct mapped
        :param cl_size: number of bytes that can be addressed individually
        :param replacement_policy: FIFO, LRU (default), MRU or RR
        :param write_back: if true (default), write back will be done on evict. Otherwise 
                           write-through is used
        :param write_allocate: if true (default), a load will be issued on a write miss
        :param write_combining: if true, this cache will combine writes and issue them on evicts 
                                (default is false)
        :param subblock_size: the minimum blocksize that write-combining can handle
        :param load_from: the cache level to forward a load in case of a load miss or
                          write-allocate, if None, assumed to be main memory
        :param store_to: the cache level to forward a store to in case of eviction of dirty lines,
                         if None, assumed to be main memory
        :param victims_to: the cache level to forward any evicted lines to (dirty or not)
        :param swap_on_load: if true, lines will be swaped between this and the higher cache level
                             (default is false). Currently not supported.
        :param parent: the cache where misses are forwarded to, if None it is a last level cache

        The total cache size is the product of sets*ways*cl_size.
        Internally all addresses are converted to cacheline indices.

        Instantization has to happen from last level cache to first level cache, since each
        subsequent level requires a reference of the other level.
        '''
        assert load_from is None or isinstance(load_from, Cache), \
            "load_from needs to be None or a Cache object."
        assert store_to is None or isinstance(store_to, Cache), \
            "store_to needs to be None or a Cache object."
        assert victims_to is None or isinstance(victims_to, Cache), \
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
        assert (write_back, write_allocate) in [(False, False), (True, True), (True, False)], \
            "Unsupported write policy, we only support write-through and non-write-allocate, " \
            "write-back and write-allocate, and write-back and non-write-allocate."
        assert write_combining and write_back and not write_allocate or not write_combining, \
            "Write combining may only be used in a cache with write-back and non-write-allocate"
        assert subblock_size is None or cl_size % subblock_size == 0, \
            "subblock_size needs to be a devisor of cl_size or None."
        # TODO check that ways only increase from higher  to lower _exclusive_ cache
        # other wise swap won't be a valid procedure to ensure exclusiveness
        # TODO check that cl_size has to be the same with exclusive an victim caches

        self.name = name
        self.replacement_policy = replacement_policy
        self.replacement_policy_id = self.replacement_policy_enum[replacement_policy]
        self.load_from = load_from
        self.store_to = store_to
        self.victims_to = victims_to
        self.swap_on_load = swap_on_load

        if subblock_size is None:
            subblock_size = cl_size

        self.backend = backend.Cache(
            name=name, sets=sets, ways=ways, cl_size=cl_size,
            replacement_policy_id=self.replacement_policy_id,
            write_back=write_back, write_allocate=write_allocate,
            write_combining=write_combining, subblock_size=subblock_size,
            load_from=self._get_backend(load_from), store_to=self._get_backend(store_to),
            victims_to=self._get_backend(victims_to),
            swap_on_load=swap_on_load)

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

    def set_load_from(self, load_from):
        '''Updates load_from in Cache and backend'''
        assert load_from is None or isinstance(load_from, Cache), \
            "load_from needs to be None or a Cache object."
        assert load_from is None or load_from.cl_size <= self.cl_size, \
            "cl_size may only increase towards main memory."
        self.load_from = load_from
        self.backend.load_from = load_from.backend

    def set_store_to(self, store_to):
        '''Updates store_to in Cache and backend'''
        assert store_to is None or isinstance(store_to, Cache), \
            "store_to needs to be None or a Cache object."
        assert store_to is None or store_to.cl_size <= self.cl_size, \
            "cl_size may only increase towards main memory."
        self.store_to = store_to
        self.backend.store_to = store_to.backend

    def __getattr__(self, key):
        if hasattr(self, "backend"):
            return getattr(self.backend, key)
        else:
            raise AttributeError("'{}' object has no attribute '{}'".format(self.__class__, key))

    def stats(self):
        assert self.backend.LOAD_count >= 0, "LOAD_count < 0"
        assert self.backend.LOAD_byte >= 0, "LOAD_byte < 0"
        assert self.backend.STORE_count >= 0, "STORE_count < 0"
        assert self.backend.STORE_byte >= 0, "STORE_byte < 0"
        assert self.backend.HIT_count >= 0, "HIT_count < 0"
        assert self.backend.HIT_byte >= 0, "HIT_byte < 0"
        assert self.backend.MISS_count >= 0, "MISS_count < 0"
        assert self.backend.MISS_byte >= 0, "MISS_byte < 0"
        assert self.backend.EVICT_count >= 0, "EVICT_count < 0"
        assert self.backend.EVICT_byte >= 0, "EVICT_byte < 0"
        return {'name': self.name,
                'LOAD_count': self.backend.LOAD_count,
                'LOAD_byte': self.backend.LOAD_byte,
                'STORE_count': self.backend.STORE_count,
                'STORE_byte': self.backend.STORE_byte,
                'HIT_count': self.backend.HIT_count,
                'HIT_byte': self.backend.HIT_byte,
                'MISS_count': self.backend.MISS_count,
                'MISS_byte': self.backend.MISS_byte,
                'EVICT_count': self.backend.EVICT_count,
                'EVICT_byte': self.backend.EVICT_byte}

    def size(self):
        return self.sets*self.ways*self.cl_size

    def __repr__(self):
        return 'Cache(name={!r}, sets={!r}, ways={!r}, cl_size={!r}, replacement_policy={!r}, write_back={!r}, write_allocate={!r}, write_combining={!r}, load_from={!r}, store_to={!r}, victims_to={!r}, swap_on_load={!r}))'.format(
            self.name, self.sets, self.ways, self.cl_size, self.replacement_policy, self.write_back,
            self.write_allocate, self.write_combining, self.load_from, self.store_to,
            self.victims_to, self.swap_on_load)


class MainMemory(object):
    def __init__(self, name=None, last_level_load=None, last_level_store=None):
        '''Creates one cache level out of given configuration.'''
        self.name = "MEM" if name is None else name

        if last_level_load is not None:
            self.load_to(last_level_load)
        else:
            self.last_level_load = None

        if last_level_store is not None:
            self.store_from(last_level_store)
        else:
            self.last_level_store = None

    def reset_stats(self):
        # since all stats in main memory are derived from the last level cache, there is nothing to
        # reset
        pass

    def load_to(self, last_level_load):
        assert isinstance(last_level_load, Cache), \
            "last_level needs to be a Cache object."
        assert last_level_load.load_from is None, \
            "last_level_load must be a last level cache (.load_from is None)."
        self.last_level_load = last_level_load

    def store_from(self, last_level_store):
        assert isinstance(last_level_store, Cache), \
            "last_level needs to be a Cache object."
        assert last_level_store.store_to is None, \
            "last_level_store must be a last level cache (.store_to is None)."
        self.last_level_store = last_level_store

    def __getattr__(self, key):
        try:
            return self.stats()[key]
        except KeyError:
            raise AttributeError

    def stats(self):
        return {'name': self.name,
                'LOAD_count': self.last_level_load.MISS_count,
                'LOAD_byte': self.last_level_load.MISS_byte,
                'STORE_count': self.last_level_store.EVICT_count,
                'STORE_byte': self.last_level_store.EVICT_byte,
                'HIT_count': self.last_level_load.MISS_count,
                'HIT_byte': self.last_level_load.MISS_byte,
                'EVICT_count': 0,
                'EVICT_byte': 0,
                'MISS_count': 0,
                'MISS_byte': 0}

    def __repr__(self):
        return 'MainMemory(last_level_load={!r}, last_level_store={!r})'.format(
            self.last_level_load, self.last_level_store)
