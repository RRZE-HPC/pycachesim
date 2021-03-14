#!/usr/bin/env python
"""Hierarchical Cache Simulator."""
from __future__ import print_function
from __future__ import division
from __future__ import unicode_literals

import textwrap
from functools import reduce
import sys
from collections import Iterable

from cachesim import backend

if sys.version_info[0] < 3:
    range = xrange


def is_power2(num):
    """Return True if num is a power of two."""
    return num > 0 and (num & (num - 1)) == 0


class CacheSimulator(object):
    """
    High-level interface to the Cache Simulator.

    This is the only class that needs to be directly interfaced to.
    """

    def __init__(self, first_level, main_memory):
        """
        Create interface to interact with cache simulator backend.

        :param first_level: first cache level object.
        :param main_memory: main memory object.
        """
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
        """Create cache hierarchy from dictionary."""
        main_memory = MainMemory()
        caches = {}

        referred_caches = set()

        # First pass, create all named caches and collect references
        for name, conf in d.items():
            caches[name] = Cache(name=name,
                                 **{k: v for k, v in conf.items()
                                    if k not in ['store_to', 'load_from', 'victims_to']})
            if 'store_to' in conf:
                referred_caches.add(conf['store_to'])
            if 'load_from' in conf:
                referred_caches.add(conf['load_from'])
            if 'victims_to' in conf:
                referred_caches.add(conf['victims_to'])

        # Second pass, connect caches
        for name, conf in d.items():
            if 'store_to' in conf and conf['store_to'] is not None:
                caches[name].set_store_to(caches[conf['store_to']])
            if 'load_from' in conf and conf['load_from'] is not None:
                caches[name].set_load_from(caches[conf['load_from']])
            if 'victims_to' in conf and conf['victims_to'] is not None:
                caches[name].set_victims_to(caches[conf['victims_to']])

        # Find first level (not target of any load_from or store_to)
        first_level = set(d.keys()) - referred_caches
        assert len(first_level) == 1, "Unable to find first cache level."
        first_level = caches[list(first_level)[0]]

        # Find last level caches (has no load_from or store_to target)
        last_level_load = c = first_level
        while c is not None:
            last_level_load = c
            c = c.load_from
        assert last_level_load is not None, "Unable to find last cache level."
        last_level_store = c = first_level
        while c is not None:
            last_level_store = c
            c = c.store_to
        assert last_level_store is not None, "Unable to find last cache level."

        # Set main memory connections
        main_memory.load_to(last_level_load)
        main_memory.store_from(last_level_store)

        return cls(first_level, main_memory), caches, main_memory

    def reset_stats(self):
        """
        Reset statistics in all cache levels.

        Use this after warming up the caches to get a steady state result.
        """
        for c in self.levels(with_mem=False):
            c.reset_stats()

    def force_write_back(self):
        """Write all pending dirty lines back."""
        # force_write_back() is acting recursive by it self, but multiple write-back first level
        # caches are imaginable. Better safe than sorry:
        for c in self.levels(with_mem=False):
            c.force_write_back()

    def load(self, addr, length=1):
        """
        Load one or more addresses.

        :param addr: byte address of load location
        :param length: All address from addr until addr+length (exclusive) are
                       loaded (default: 1)
        """
        if addr is None:
            return
        elif not isinstance(addr, Iterable):
            self.first_level.load(addr, length=length)
        else:
            self.first_level.iterload(addr, length=length)

    def store(self, addr, length=1, non_temporal=False):
        """
        Store one or more adresses.

        :param addr: byte address of store location
        :param length: All address from addr until addr+length (exclusive) are
                       stored (default: 1)
        :param non_temporal: if True, no write-allocate will be issued, but cacheline will be zeroed
        """
        if non_temporal:
            raise ValueError("non_temporal stores are not yet supported")

        if addr is None:
            return
        elif not isinstance(addr, Iterable):
            self.first_level.store(addr, length=length)
        else:
            self.first_level.iterstore(addr, length=length)

    def loadstore(self, addrs, length=1):
        """
        Load and store address in order given.

        :param addrs: iteratable of address tuples: [(loads, stores), ...]
        :param length: will load and store all bytes between addr and
                       addr+length (for each address)
        """
        if not isinstance(addrs, Iterable):
            raise ValueError("addr must be iteratable")
        self.first_level.loadstore(addrs, length=length)

    def stats(self):
        """Collect all stats from all cache levels."""
        for c in self.levels():
            yield c.stats()

    def print_stats(self, header=True, file=sys.stdout):
        """Pretty print stats table."""
        if header:
            print("CACHE {:*^18} {:*^18} {:*^18} {:*^18} {:*^18}".format(
                "HIT", "MISS", "LOAD", "STORE", "EVICT"), file=file)
        for s in self.stats():
            print("{name:>5} {HIT_count:>6} ({HIT_byte:>8}B) {MISS_count:>6} ({MISS_byte:>8}B) "
                  "{LOAD_count:>6} ({LOAD_byte:>8}B) {STORE_count:>6} "
                  "({STORE_byte:>8}B) {EVICT_count:>6} ({EVICT_byte:>8}B)".format(
                    HIT_bytes=2342, **s),
                  file=file)

    def levels(self, with_mem=True):
        """Return cache levels, optionally including main memory."""
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

    def count_invalid_entries(self):
        """Sum of all invalid entry counts from cache levels."""
        return sum([c.count_invalid_entries() for c in self.levels(with_mem=False)])

    def mark_all_invalid(self):
        """Mark all entries invalid and reset stats."""
        for c in self.levels(with_mem=False):
            c.mark_all_invalid()
        self.reset_stats()

    # def draw_array(self, start, width, height, block=1):
    #     """Return image representation of cache states."""
    #     length = (width*height)//block
    #     canvas = Image.new("RGB", (width, height))
    #     # FIXME: switch to palette "P" with ImagePalette
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

    def __repr__(self, recursion=True):
        """Return string representation of object."""
        first_level_repr = self.first_level.__repr__(recursion=recursion)
        main_memory_repr = self.main_memory.__repr__(recursion=recursion)
        return 'CacheSimulator({}, {})'.format(first_level_repr, main_memory_repr)


def get_backend(cache):
    """Return backend of *cache* unless *cache* is None, then None is returned."""
    if cache is not None:
        return cache.backend
    return None


class Cache(object):
    """Cache level object."""

    replacement_policy_enum = {"FIFO": 0, "LRU": 1, "MRU": 2, "RR": 3}

    def __init__(self, name, sets, ways, cl_size,
                 replacement_policy="LRU",
                 write_back=True,
                 write_allocate=True,
                 write_combining=False,
                 subblock_size=None,
                 load_from=None, store_to=None, victims_to=None,
                 swap_on_load=False):
        """Create one cache level out of given configuration.

        :param sets: total number of sets, if 1 cache will be full-associative
        :param ways: total number of ways, if 1 cache will be direct mapped
        :param cl_size: number of bytes that can be addressed individually
        :param replacement_policy: FIFO, LRU (default), MRU or RR
        :param write_back: if true (default), write back will be done on evict.
                           Otherwise write-through is used
        :param write_allocate: if true (default), a load will be issued on a
                               write miss
        :param write_combining: if true, this cache will combine writes and
                                issue them on evicts(default is false)
        :param subblock_size: the minimum blocksize that write-combining can
                              handle
        :param load_from: the cache level to forward a load in case of a load
                          miss or write-allocate, if None, assumed to be main
                          memory
        :param store_to: the cache level to forward a store to in case of
                         eviction of dirty lines, if None, assumed to be main
                         memory
        :param victims_to: the cache level to forward any evicted lines to
                           (dirty or not)
        :param swap_on_load: if true, lines will be swaped between this and the
                             higher cache level (default is false).
                             Currently not supported.

        The total cache size is the product of sets*ways*cl_size.
        Internally all addresses are converted to cacheline indices.

        Instantization has to happen from last level cache to first level
        cache, since each subsequent level requires a reference of the other
        level.
        """
        assert load_from is None or isinstance(load_from, Cache), \
            "load_from needs to be None or a Cache object."
        assert store_to is None or isinstance(store_to, Cache), \
            "store_to needs to be None or a Cache object."
        assert victims_to is None or isinstance(victims_to, Cache), \
            "victims_to needs to be None or a Cache object."
        assert is_power2(cl_size), \
            "cl_size needs to be a power of two."
        assert store_to is None or store_to.cl_size >= cl_size, \
            "cl_size may only increase towards main memory."
        assert load_from is None or load_from.cl_size >= cl_size, \
            "cl_size may only increase towards main memory."
        assert replacement_policy in self.replacement_policy_enum, \
            "Unsupported replacement strategy, we only support: " + \
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
            load_from=get_backend(load_from), store_to=get_backend(store_to),
            victims_to=get_backend(victims_to),
            swap_on_load=swap_on_load)

    def get_cl_start(self, addr):
        """Return first address belonging to the same cacheline as *addr*."""
        return addr >> self.backend.cl_bits << self.backend.cl_bits

    def get_cl_end(self, addr):
        """Return last address belonging to the same cacheline as *addr*."""
        return self.get_cl_start(addr) + self.backend.cl_size - 1

    def set_load_from(self, load_from):
        """Update load_from in Cache and backend."""
        assert load_from is None or isinstance(load_from, Cache), \
            "load_from needs to be None or a Cache object."
        assert load_from is None or load_from.cl_size <= self.cl_size, \
            "cl_size may only increase towards main memory."
        self.load_from = load_from
        self.backend.load_from = load_from.backend

    def set_store_to(self, store_to):
        """Update store_to in Cache and backend."""
        assert store_to is None or isinstance(store_to, Cache), \
            "store_to needs to be None or a Cache object."
        assert store_to is None or store_to.cl_size <= self.cl_size, \
            "cl_size may only increase towards main memory."
        self.store_to = store_to
        self.backend.store_to = store_to.backend

    def set_victims_to(self, victims_to):
        """Update victims_to in Cache and backend."""
        assert victims_to is None or isinstance(victims_to, Cache), \
            "store_to needs to be None or a Cache object."
        assert victims_to is None or victims_to.cl_size == self.cl_size, \
            "cl_size may only increase towards main memory."
        self.victims_to = victims_to
        self.backend.victims_to = victims_to.backend

    def __getattr__(self, key):
        """Return cache attribute, preferably to backend."""
        if "backend" in self.__dict__:
            return getattr(self.backend, key)
        else:
            raise AttributeError("'{}' object has no attribute '{}'".format(self.__class__, key))

    def stats(self):
        """Return dictionay with all stats at this level."""
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
        """Return total cache size."""
        return self.sets * self.ways * self.cl_size

    def __repr__(self, recursion=False):
        """Return string representation of object."""
        if recursion:
            load_from_repr, store_to_repr, victims_to_repr = map(
                lambda c: c.__repr__(recursion=True) if c is not None else 'None',
                [self.load_from, self.store_to, self.victims_to])
        else:
            load_from_repr = self.load_from.name if self.load_from is not None else 'None'
            store_to_repr = self.store_to.name if self.store_to is not None else 'None'
            victims_to_repr = self.victims_to.name if self.victims_to is not None else 'None'
        return ('Cache(name={!r}, sets={!r}, ways={!r}, cl_size={!r}, replacement_policy={!r}, '
                'write_back={!r}, write_allocate={!r}, write_combining={!r}, load_from={}, '
                'store_to={}, victims_to={}, swap_on_load={!r})').format(
            self.name, self.sets, self.ways, self.cl_size, self.replacement_policy,
            self.write_back, self.write_allocate, self.write_combining, load_from_repr,
            store_to_repr, victims_to_repr, self.swap_on_load)


class MainMemory(object):
    """Main memory object. Last level of cache hierarchy, able to hit on all requests."""

    def __init__(self, name=None, last_level_load=None, last_level_store=None):
        """Create one cache level out of given configuration."""
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
        """Dummy, no stats need to be reset in main memory."""
        # since all stats in main memory are derived from the last level cache, there is nothing to
        # reset
        pass

    def load_to(self, last_level_load):
        """Set level where to load from."""
        assert isinstance(last_level_load, Cache), \
            "last_level needs to be a Cache object."
        assert last_level_load.load_from is None, \
            "last_level_load must be a last level cache (.load_from is None)."
        self.last_level_load = last_level_load

    def store_from(self, last_level_store):
        """Set level where to store to."""
        assert isinstance(last_level_store, Cache), \
            "last_level needs to be a Cache object."
        assert last_level_store.store_to is None, \
            "last_level_store must be a last level cache (.store_to is None)."
        self.last_level_store = last_level_store

    def __getattr__(self, key):
        """Return cache attribute, preferably to backend."""
        try:
            return self.stats()[key]
        except KeyError:
            raise AttributeError

    def stats(self):
        """Return dictionay with all stats at this level."""
        load_count = self.last_level_load.MISS_count
        load_byte = self.last_level_load.MISS_byte

        if self.last_level_load.victims_to is not None:
            # If there is a victim cache between last_level and memory, subtract all victim hits
            load_count -= self.last_level_load.victims_to.HIT_count
            load_byte -= self.last_level_load.victims_to.HIT_byte

        return {'name': self.name,
                'LOAD_count': load_count,
                'LOAD_byte': load_byte,
                'HIT_count': load_count,
                'HIT_byte': load_byte,
                'STORE_count': self.last_level_store.EVICT_count,
                'STORE_byte': self.last_level_store.EVICT_byte,
                'EVICT_count': 0,
                'EVICT_byte': 0,
                'MISS_count': 0,
                'MISS_byte': 0}

    def __repr__(self, recursion=False):
        """Return string representation of object."""
        if recursion:
            last_level_load_repr, last_level_store_repr = map(
                lambda c: c.__repr__(recursion=True) if c is not None else 'None',
                [self.last_level_load, self.last_level_store])
        else:
            last_level_load_repr, last_level_store_repr = map(
                lambda c: c.name if c is not None else 'None',
                [self.last_level_load, self.last_level_store])

        return 'MainMemory(last_level_load={}, last_level_store={})'.format(
            last_level_load_repr, last_level_store_repr)


class CacheVisualizer(object):
    """Visualize cache state by generation of VTK files."""

    def __init__(self, cs, dims, start_address=0, element_size=8, filename_base=None):
        """
        Create interface to interact with cache visualizer.

        :param cs: CacheSimulator object.
        :param dims: dimensions at which you wish to visualize the data
                     for eg. [10,15]. tells visualize a 2d array of
                     10 rows and 15 columns of elements having wordSize.
        :param start_address: starting address of the array.
        :param element_size: size of each element in bytes.
        :param filename_base: base name of VTK file to be outputed for Paraview.

        """
        assert isinstance(cs, CacheSimulator), \
            "cs needs to be a CacheSimulator object."

        ndim = len(dims)
        assert ndim < 3, "Currently dump and view supported up to 3-D arrays only"

        self.dims = dims
        self.npts = reduce(int.__mul__, self.dims, 1)

        self.cs = cs
        self.startAddress = start_address
        self.element_size = element_size
        self.filename_base = filename_base
        self.count = 0

    def dump_state(self):
        vtk_str = textwrap.dedent("""\
        # vtk DataFile Version 4.0
        CACHESIM VTK output
        ASCII
        DATASET STRUCTURED_POINTS
        """)

        # dimension string needs to be reversed and padded to 3 dimensions (using 1s)
        dim_str = " ".join([str(d+1) for d in reversed((self.dims + [1, 1, 1])[:3])])

        vtk_str += textwrap.dedent("""\
        DIMENSIONS {}
        ORIGIN 0 0 0
        SPACING 1 1 1
        CELL_DATA {}
        FIELD DATA 1
        """).format(dim_str, self.npts)

        ctr = 1
        data = []
        for c in self.cs.levels(with_mem=False):
            address = [0] * self.npts
            cached_addresses = {x - self.startAddress for x in c.backend.cached}
            # Filtering elements outside of scope and scaling address to element indices
            cached_elements = {x // self.element_size for x in cached_addresses
                               if 0 <= x < self.npts * self.element_size}
            for a in cached_elements:
                address[a] = 1
            data.append(address)
            ctr += 1

        total_levels = (ctr - 1)
        vtk_str += "\nData_arr {} {} double\n".format(total_levels, self.npts)

        for i in range(self.npts):
            vtk_str += " ".join([str(d[i]) for d in data])
            vtk_str += "\n"

        if self.filename_base is None:
            file = sys.stdout
        else:
            file = open("{}_{}.vtk".format(self.filename_base, self.count), 'w')
        file.write(vtk_str)
        file.flush()
        if file != sys.stdout:
            file.close()

        self.count += 1
