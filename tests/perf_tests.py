#!/usr/bin/env python
from __future__ import print_function
from __future__ import absolute_import

import time
import sys
import cProfile
sys.path[0:0] = ['.', '..']

from cachesim import CacheSimulator, Cache


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
    def __enter__(self):
        self.start = time.clock()
        return self

    def __exit__(self, *args):
        self.end = time.clock()
        self.interval = self.end - self.start


class TimingTests:
    def time_load1000_tiny(self):
        l3 = Cache(4, 8, 8, "LRU")
        l2 = Cache(4, 4, 8, "LRU", parent=l3)
        l1 = Cache(2, 4, 8, "LRU", parent=l2)
        mh = CacheSimulator(l1)
        
        with Timer() as t:
            mh.load(0, 1000)
        return t.interval
    
    def time_load10000_tiny(self):
        l3 = Cache(4, 8, 8, "LRU")
        l2 = Cache(4, 4, 8, "LRU", parent=l3)
        l1 = Cache(2, 4, 8, "LRU", parent=l2)
        mh = CacheSimulator(l1)
        
        with Timer() as t:
            mh.load(0, 10000)
        return t.interval
    
    def time_load100000_tiny(self):
        l3 = Cache(4, 8, 8, "LRU")
        l2 = Cache(4, 4, 8, "LRU", parent=l3)
        l1 = Cache(2, 4, 8, "LRU", parent=l2)
        mh = CacheSimulator(l1)
        
        with Timer() as t:
            mh.load(0, 100000)
        return t.interval
    
    def time_load100000_tiny_collisions(self):
        l3 = Cache(4, 8, 8, "LRU")
        l2 = Cache(4, 4, 8, "LRU", parent=l3)
        l1 = Cache(2, 4, 8, "LRU", parent=l2)
        mh = CacheSimulator(l1)
        mh.load(0, 100000)
        
        with Timer() as t:
            mh.load(0, 100000)
        return t.interval

    @do_cprofile
    def time_load1000000(self):
        l3 = Cache(4096, 1024, 8, "LRU")
        l2 = Cache(4096, 8, 8, "LRU", parent=l3)
        l1 = Cache(512, 8, 8, "LRU", parent=l2)
        mh = CacheSimulator(l1)
        
        with Timer() as t:
            mh.load(0, 1000000)
        return t.interval

    def time_load1000000_collisions(self):
        l3 = Cache(4096, 1024, 8, "LRU")
        l2 = Cache(4096, 8, 8, "LRU", parent=l3)
        l1 = Cache(512, 8, 8, "LRU", parent=l2)
        mh = CacheSimulator(l1)
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
