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

if sys.version_info[0] < 3:
    range = xrange


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
    
    def copy(self):
        '''Returns a copy of this object and all associated cache levels'''
        return deepcopy(self)
    
    def __repr__(self):
        return 'CacheSimulator({!r})'.format(self.first_level)


class Cache(object):
    def __init__(self, sets, ways, cl_size, replacement, parent=None):
        '''Creates one cache level out of given configuration.
    
        :param sets: total number of sets, if 1 cache will be full-associative
        :param ways: total number of ways, if 1 cache will be direct mapped
        :param cl_size: number of bytes that can be addressed individually
        :param mapping: mapping from memory addresses to cache locations
        :param parent: the cache where misses are forwarded to, if None it is a last level cache
        
        The total cache size is the product of sets*ways*cl_size.
        Internally all addresses are converted to cacheline indices, if a cl_size > 1 is set.
        '''
        assert parent is None or isinstance(parent, Cache), \
            "parent needs to be None or a Cache object."
        assert isinstance(replacement, ReplacementPolicy), \
            "replacement needs to be a ReplacementPolicy object."
        assert is_power2(cl_size), \
            "cl_size needs to be a power of two."
        assert parent is None or parent.cl_size <= cl_size, \
            "cl_size may only increase towards main memory."
        assert is_power2(ways), "ways needs to be a power of 2"
        
        self.sets = sets
        self.ways = ways
        self.way_bits = int(math.log(ways, 2))
        self.cl_size = cl_size
        self.cl_bits = int(math.log(cl_size, 2))
        self.replacement = replacement
        self.parent = parent
        
        self.placement = defaultdict(set)
        self.enable_stats()
    
    def disable_stats(self):
        self.stats = None
    
    def enable_stats(self):
        self.stats = {
            'LOAD': 0,
            'STORE': 0,
            'HIT': 0,
            'MISS': 0}

    def _get_set_id(self, addr):
        '''Returns the set id associated with the address
        
        This also works for negative (relative) addresses/offsets.'''
        return (int(addr) >> self.way_bits) & (self.sets-1)
    
    def _addr_to_cl(self, addr):
        '''Returns a cacheline where the cl_size has be applied'''
        return addr >> self.cl_bits

    @profile
    def load(self, addr):
        '''Load one element into the cache.
        
        This function takes care of hit vs miss and coordination with the replacement policy and 
        placement locations. It also passes along the request to its parent.
        '''
        # Increase LOAD counter
        stats = self.stats
        if stats is not None:
            stats['LOAD'] += 1
        
        cl = self._addr_to_cl(addr)
        set_id = self._get_set_id(cl)
        set_data = self.placement[set_id]
        
        if cl not in set_data:
        ##if way_idx is None:
            # Increase MISS counter
            if stats is not None:
                stats['MISS'] += 1
            
            # Load data from parent (unless parent is main memory):
            if self.parent is not None:
                self.parent.load(addr)
            
            if len(set_data) == self.ways:
                # Set is already full
                # Use mapping to place the data and replace something else
                victim = self.replacement.replace(cl, set_id)
            else:
                # Set has some space left, just load
                self.replacement.load(cl, set_id)
                victim = None
        
            # Put addr into cache:
            #self.cached.add(addr)
            set_data.add(cl)
            if victim is not None:
                # Remove victim from cache (it was replaced)
                set_data.remove(victim)
        else:
            # Increase HIT counter
            if stats is not None:
                stats['HIT'] += 1
            
            # Inform mapping and replacement of the load
            self.replacement.load(cl, set_id)
        
        if stats is not None:
            self.stats = stats

    def store(self, addr):
        # Increase STORE counter
        if self.stats is not None:
            self.stats['STORE'] += 1
        
        cl = self._addr_to_cl(addr)
        set_id = self._get_set_id(cl)
        
        # Parent needs to store aswell (unless parent is main memory):
        if self.parent is not None:
            self.parent.store(addr)
        
        self.replacement.store(cl, set_id)
        
        # TODO handle write allocate
    
    def size(self):
        return self.sets*self.ways*self.cl_size
    
    def used_size(self):
        '''Returns the number of cached bytes.'''
        return reduce(operator.add, map(len, self.placement.values()))
    
    def full(self):
        '''Returns True if cache is full'''
        return self.used_size() == self.size()
    
    @property
    def cached(self):
        cls_cached = reduce(set.union, self.placement.values(), set())
        addrs_cached = map(lambda cl: set(range(cl*self.cl_size, (cl+1)*self.cl_size)), cls_cached)
        return reduce(set.union, addrs_cached, set())
    
    def __repr__(self):
        return 'Cache(sets={!r}, ways={!r}, cl_size={!r}, replacement={!r}, parent={!r})'.format(
            self.sets, self.ways, self.cl_size, self.replacement, self.parent)


class ReplacementPolicy(object):
    '''Base class for replacement policies'''
    def load(self, addr, set_id=None):
        raise NotImplementedError()

    def store(self, addr, set_id=None):
        raise NotImplementedError()

    def replace(self, addr, set_id=None):
        raise NotImplementedError()


class LRUPolicy(ReplacementPolicy):
    def __init__(self):
        self.access_history = defaultdict(list)
    
    def load(self, addr, set_id=None):
        '''Updates access history for addr in set set_id.'''
        set_data = self.access_history[set_id]
        if addr in set_data:
            set_data.remove(addr)
        set_data.append(addr)

    # Do we need to track store accesses in LRU?
    def store(self, addr, set_id=None):
        '''Updates access history for addr in set set_id.'''
        set_data = self.access_history[set_id]
        if addr in set_data:
            set_data.remove(addr)
        set_data.append(addr)

    def replace(self, addr, set_id=None):
        '''Returns address of to be replaced location.
        
        Victims is the list of addresses that can be replaced.
        '''
        victim = self.access_history[set_id].pop(0)
        self.access_history[set_id].append(addr)
        return victim
    
    def __repr__(self):
        return 'LRUPolicy()'


def is_power2(num):
    return num > 0 and (num & (num - 1)) == 0


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