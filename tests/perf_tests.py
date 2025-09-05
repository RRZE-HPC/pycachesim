#!/usr/bin/env python
from __future__ import print_function
from __future__ import absolute_import

import time
import sys
import cProfile
#sys.path[0:0] = ['.', '..']

from cachesim import CacheSimulator, Cache, MainMemory


def do_cprofile(func):
    """Originally from https://zapier.com/engineering/profiling-python-boss/"""
    def profiled_func(*args, **kwargs):
        profile = cProfile.Profile()
        try:
            profile.enable()
            result = func(*args, **kwargs)
            profile.disable()
            return result
        finally:
            profile.print_stats()
    return profiled_func


class Timer:
    mytime = None
    if sys.version_info >= (3,3):
        mytime = time.monotonic
    else:
        mytime = time.clock
    def __enter__(self):
        self.start = self.mytime()
        return self

    def __exit__(self, *args):
        self.end = self.mytime()
        self.interval = self.end - self.start


class TimingTests:
    def time_load1000_tiny(self):
        l3 = Cache("L3", 4, 8, 8, "LRU")
        l2 = Cache("L2", 4, 4, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", store_to=l2, load_from=l2)
        mem = MainMemory("MEM", l3, l3)
        mh = CacheSimulator(l1, mem)
        
        with Timer() as t:
            mh.load(0, 1000)
        return t.interval
    
    def time_load10000_tiny(self):
        l3 = Cache("L3", 4, 8, 8, "LRU")
        l2 = Cache("L2", 4, 4, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", store_to=l2, load_from=l2)
        mem = MainMemory("MEM", l3, l3)
        mh = CacheSimulator(l1, mem)
        
        with Timer() as t:
            mh.load(0, 10000)
        return t.interval
    
    def time_load100000_tiny(self):
        l3 = Cache("L3", 4, 8, 8, "LRU")
        l2 = Cache("L2", 4, 4, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", store_to=l2, load_from=l2)
        mem = MainMemory("MEM", l3, l3)
        mh = CacheSimulator(l1, mem)
        
        with Timer() as t:
            mh.load(0, 100000)
        return t.interval
    
    def time_load100000_tiny_collisions(self):
        l3 = Cache("L3", 4, 8, 8, "LRU")
        l2 = Cache("L2", 4, 4, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 2, 4, 8, "LRU", store_to=l2, load_from=l2)
        mem = MainMemory("MEM", l3, l3)
        mh = CacheSimulator(l1, mem)
        mh.load(0, 100000)
        
        with Timer() as t:
            mh.load(0, 100000)
        return t.interval

    @do_cprofile
    def time_load1000000(self):
        l3 = Cache("L3", 4096, 1024, 8, "LRU")
        l2 = Cache("L2", 4096, 8, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 512, 8, 8, "LRU", store_to=l2, load_from=l2)
        mem = MainMemory("MEM", l3, l3)
        mh = CacheSimulator(l1, mem)
        
        with Timer() as t:
            mh.load(0, 1000000)
        return t.interval

    def time_load1000000_collisions(self):
        l3 = Cache("L3", 4096, 1024, 8, "LRU")
        l2 = Cache("L2", 4096, 8, 8, "LRU", store_to=l3, load_from=l3)
        l1 = Cache("L1", 512, 8, 8, "LRU", store_to=l2, load_from=l2)
        mem = MainMemory("MEM", l3, l3)
        mh = CacheSimulator(l1, mem)
        mh.load(0, 1000000)
        
        with Timer() as t:
            mh.load(0, 1000000)
        return t.interval

    def run(self):
        print("{:>40} | {:<10}".format("Function", "Time (s)"))
        print("-"*40+" | "+"-"*10)
        for k, f in sorted(self.__class__.__dict__.items()):
            if k.startswith('time_'):
                ret = f(self)
                if ret:
                    print("{:>40} | {:>10.4f}".format(k, ret))
                else:
                    print("{:>40} | {:>10}".format(k, "ERROR"))
        

if __name__ == '__main__':
    TimingTests().run()
