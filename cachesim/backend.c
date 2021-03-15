#ifndef NO_PYTHON
    #include "Python.h"
    #include <structmember.h>
#endif

// #include <stdlib.h>
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifndef NO_PYTHON
struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#endif

#if PY_MAJOR_VERSION < 3
static PyMethodDef cachesim_methods[] = {
    {NULL, NULL}
};
#endif

#endif


#ifndef NO_PYTHON
static void Cache_dealloc(Cache* self) {
    Py_XDECREF(self->store_to);
    Py_XDECREF(self->load_from);
    //Py_XDECREF(self->victims_to);
    PyMem_Del(self->placement);
    Py_TYPE(self)->tp_free((PyObject*)self);
}
#endif

long log2_uint(unsigned long x) {
    long ans = 0;
    while(x >>= 1) {
        ans++;
    }
    return ans;
}

int isPowerOfTwo(long x) {
    return ((x != 0) && !(x & (x - 1)));
}

#ifndef NO_PYTHON
static PyMemberDef Cache_members[] = {
    {"name", T_STRING, offsetof(Cache, name), 0,
     "name of cache level"},
    {"sets", T_LONG, offsetof(Cache, sets), 0,
     "number of sets available"},
    {"ways", T_LONG, offsetof(Cache, ways), 0,
     "number of ways available"},
    {"cl_size", T_LONG, offsetof(Cache, cl_size), 0,
     "number of bytes in a cacheline"},
    {"cl_bits", T_LONG, offsetof(Cache, cl_bits), 0,
     "number of bits used to identiy individual bytes in a cacheline"},
    {"subblock_size", T_LONG, offsetof(Cache, subblock_size), 0,
     "number of bytes per subblock (must be a devisor of cl_size)"},
    {"subblock_bits", T_LONG, offsetof(Cache, subblock_bits), 0,
     "number of bits needed to identify subblocks (= number of subblocks per cacheline)"},
    {"replacement_policy_id", T_INT, offsetof(Cache, replacement_policy_id), 0,
     "replacement strategy of cachlevel"},
    {"write_back", T_INT, offsetof(Cache, write_back), 0,
     "write back of cachlevel (0 is write-through, 1 is write-back)"},
    {"write_allocate", T_INT, offsetof(Cache, write_allocate), 0,
     "write allocate of cachlevel (0 is non-write-allocate, 1 is write-allocate)"},
    {"write_combining", T_INT, offsetof(Cache, write_combining), 0,
     "combine writes on this level, before passing them on"},
    {"load_from", T_OBJECT, offsetof(Cache, load_from), 0,
     "load parent Cache object (cache level which is closer to main memory)"},
    {"store_to", T_OBJECT, offsetof(Cache, store_to), 0,
     "store parent Cache object (cache level which is closer to main memory)"},
    {"victims_to", T_OBJECT, offsetof(Cache, victims_to), 0,
     "Cache object where victims will be send to (closer to main memory, None if victims vanish)"},
    {"LOAD_count", T_LONGLONG, offsetof(Cache, LOAD.count), 0,
     "number of loads performed"},
    {"LOAD_byte", T_LONGLONG, offsetof(Cache, LOAD.byte), 0,
     "number of bytes loaded"},
    {"STORE_count", T_LONGLONG, offsetof(Cache, STORE.count), 0,
     "number of stores performed"},
    {"STORE_byte", T_LONGLONG, offsetof(Cache, STORE.byte), 0,
     "number of bytes stored"},
    {"HIT_count", T_LONGLONG, offsetof(Cache, HIT.count), 0,
     "number of cache hits"},
    {"HIT_byte", T_LONGLONG, offsetof(Cache, HIT.byte), 0,
     "number of bytes that were cache hits"},
    {"MISS_count", T_LONGLONG, offsetof(Cache, MISS.count), 0,
     "number of misses"},
    {"MISS_byte", T_LONGLONG, offsetof(Cache, MISS.byte), 0,
     "number of bytes missed"},
    {"EVICT_count", T_LONGLONG, offsetof(Cache, EVICT.count), 0,
     "number of evicts"},
    {"EVICT_byte", T_LONGLONG, offsetof(Cache, EVICT.byte), 0,
     "number of bytes evicted"},
    {"verbosity", T_INT, offsetof(Cache, verbosity), 0,
     "verbosity level of output"},
    {NULL}  /* Sentinel */
};
#endif

inline static long Cache__get_cacheline_id(Cache* self, long addr) {
    return addr >> self->cl_bits;
}

inline static long Cache__get_set_id(Cache* self, long cl_id) {
    return cl_id % self->sets;
}

inline static addr_range __range_from_addrs(long addr, long last_addr) {
    addr_range range;
    range.addr = addr;
    range.length = last_addr-addr-1;
    return range;
}

inline static long Cache__get_addr_from_cl_id(Cache* self, long cl_id) {
    return cl_id << self->cl_bits;
}

inline static addr_range Cache__get_range_from_cl_id(Cache* self, long cl_id) {
    addr_range range;
    range.addr = Cache__get_addr_from_cl_id(self, cl_id);
    range.length = self->cl_size;
    return range;
}

inline static addr_range Cache__get_range_from_cl_id_and_range(
        Cache* self, long cl_id, addr_range range) {
    // Creates a range from a cacheline id and another range.
    // The returned range will always be a subset of (or at most the same as) the given range
    addr_range new_range;
    new_range.addr = Cache__get_addr_from_cl_id(self, cl_id);
    new_range.addr = new_range.addr > range.addr ? new_range.addr : range.addr;
    new_range.length = new_range.addr+self->cl_size < range.addr+range.length ?
        self->cl_size : range.addr+range.length-new_range.addr;
    return new_range;
}

inline static int Cache__get_location(Cache* self, long cl_id, long set_id) {
    // Returns the location a cacheline has in a cache
    // if cacheline is not present, returns -1
    // TODO use sorted data structure for faster searches in case of large number of
    // ways or full-associativity?

    for(long i=0; i<self->ways; i++) {
        if(self->placement[set_id*self->ways+i].invalid == 0 &&
           self->placement[set_id*self->ways+i].cl_id == cl_id) {
            return i;
        }
    }

    return -1; // Not found
}

void Cache__store(Cache* self, addr_range range, int non_temporal);

static int Cache__inject(Cache* self, cache_entry* entry) {
    /*
    Injects a cache entry into a cache and handles all side effects that might occur:
     - choose replacement according to policy
     - reorder queues
     - inform victim caches
     - handle write-back on replacement
    */
    long set_id = Cache__get_set_id(self, entry->cl_id);

    // Get cacheline id to be replaced according to replacement strategy
    int replace_idx;
    cache_entry replace_entry;

    if(self->replacement_policy_id == 0 || self->replacement_policy_id == 1) {
        // FIFO: replace end of queue
        // LRU: replace end of queue
        replace_idx = 0;
        replace_entry = self->placement[set_id*self->ways+self->ways-1];

        // Reorder queue
        for(long i=self->ways-1; i>0; i--) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i-1];

            // Reorder bitfild in accordance to queue
            if(self->write_combining == 1) {
                for(long j=0; j<self->subblock_bits; j++) {
                    if(BITTEST(self->subblock_bitfield, set_id*self->ways*self->subblock_bits +
                               (i-1)*self->subblock_bits + j)) {
                        BITSET(self->subblock_bitfield, set_id*self->ways*self->subblock_bits +
                               i*self->subblock_bits + j);
                    } else {
                        BITCLEAR(self->subblock_bitfield, set_id*self->ways*self->subblock_bits +
                                 i*self->subblock_bits + j);
                    }
                }
            }
        }
    } else if(self->replacement_policy_id == 2) {
        // MRU: replace first of queue
        replace_idx = self->ways-1;
        replace_entry = self->placement[set_id*self->ways];

        // Reorder queue
        for(long i=0; i>self->ways-1; i++) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i+1];

            // Reorder bitfild in accordance to queue
            if(self->write_combining == 1) {
                for(long j=0; j<self->subblock_bits; j++) {
                    if(BITTEST(self->subblock_bitfield, set_id*self->ways*self->subblock_bits +
                               (i+1)*self->subblock_bits + j)) {
                        BITSET(self->subblock_bitfield, set_id*self->ways*self->subblock_bits +
                               i*self->subblock_bits + j);
                    } else {
                        BITCLEAR(self->subblock_bitfield, set_id*self->ways*self->subblock_bits +
                                 i*self->subblock_bits + j);
                    }
                }
            }
        }
    } else { // if(self->replacement_policy_id == 3) {
        // RR: replace random element
        replace_idx = rand() & (self->ways - 1);
        replace_entry = self->placement[set_id*self->ways+replace_idx];
    }

    // Replace other cacheline according to replacement strategy (using placement order as state)
    self->placement[set_id*self->ways+replace_idx] = *entry;
#ifndef NO_PYTHON
    if(self->verbosity >= 3) {
        PySys_WriteStdout(
            "%s REPLACED cl_id=%li invalid=%u dirty=%u\n",
            self->name, replace_entry.cl_id, replace_entry.invalid, replace_entry.dirty);
    }
#endif

    // ignore invalid cache lines for write-back or victim cache
    if(replace_entry.invalid == 0) {
        // write-back: check for dirty bit of replaced and inform next lower level of store
        if(self->write_back == 1 && replace_entry.dirty == 1) {
            self->EVICT.count++;
            self->EVICT.byte += self->cl_size;
#ifndef NO_PYTHON
            if(self->verbosity >= 3) {
                PySys_WriteStdout(
                    "%s EVICT cl_id=%li invalid=%u dirty=%u\n",
                    self->name, replace_entry.cl_id, replace_entry.invalid, replace_entry.dirty);
            }
#endif
            if(self->store_to != NULL) {
                int non_temporal = 0; // default for non write-combining caches

                if(self->write_combining == 1) {
                    // Check if non-temporal store may be used or write-allocate is necessary
                    non_temporal = 1;
                    for(long i=0; i<self->subblock_bits; i++) {
                        if(BITTEST(self->subblock_bitfield,
                                set_id*self->ways*self->subblock_bits +
                                replace_idx*self->subblock_bits + i)) {
                            // incomplete cacheline, thus write-allocate is necessary
                            non_temporal = 0;
                        }
                        // Clear bits for future use
                        BITCLEAR(self->subblock_bitfield,
                                set_id*self->ways*self->subblock_bits +
                                replace_idx*self->subblock_bits + i);
                    }
                }
#ifndef NO_PYTHON
                Py_INCREF(self->store_to);
#endif
                // TODO addrs vs cl_id is not nicely solved here
                Cache__store(
                    (Cache*)self->store_to,
                    Cache__get_range_from_cl_id(self, replace_entry.cl_id),
                    non_temporal);
#ifndef NO_PYTHON
                Py_DECREF(self->store_to);
#endif
            } // else last-level-cache
        } else if(self->victims_to != NULL) {
            // Deliver replaced cacheline to victim cache, if neither dirty or already write_back
            // (if it were dirty, it would have been written to store_to if write_back is enabled)
#ifndef NO_PYTHON
            Py_INCREF(self->victims_to);
#endif
            // Inject into victims_to
            Cache* victims_to = (Cache*)self->victims_to;
            Cache__inject(victims_to, &replace_entry);
            // Take care to include into evict stats
            self->EVICT.count++;
            self->EVICT.byte += self->cl_size;
            victims_to->STORE.count++;
            victims_to->STORE.byte += self->cl_size;
#ifndef NO_PYTHON
            Py_DECREF(self->victims_to);
#endif
        }
    }

    return replace_idx;
}

int Cache__load(Cache* self, addr_range range) {
    /*
    Signals request of addr range by higher level. This handles hits and misses.
    */
    self->LOAD.count++;
    self->LOAD.byte += range.length;
    int placement_idx = -1;

    // Handle range:
    long last_cl_id = Cache__get_cacheline_id(self, range.addr+range.length-1);
    for(long cl_id=Cache__get_cacheline_id(self, range.addr); cl_id<=last_cl_id; cl_id++) {
        long set_id = Cache__get_set_id(self, cl_id);
#ifndef NO_PYTHON
        if(self->verbosity >= 4) {
            PySys_WriteStdout(
                "%s LOAD=%lli addr=%li length=%li cl_id=%li set_id=%li\n",
                self->name, self->LOAD.count, range.addr, range.length, cl_id, set_id);
        }
#endif

        // Check if cl_id is already cached
        int location = Cache__get_location(self, cl_id, set_id);
        if(location != -1) {
            // HIT: Found it!
            self->HIT.count++;
            // We only add actual bytes that were requested to hit.byte
            self->HIT.byte += self->cl_size < range.length ? self->cl_size : range.length;
#ifndef NO_PYTHON
            if(self->verbosity >= 3) {
                PySys_WriteStdout("%s HIT self->LOAD=%lli addr=%li cl_id=%li set_id=%li\n",
                                  self->name, self->LOAD.count, range.addr, cl_id, set_id);
            }
#endif

            cache_entry entry = self->placement[set_id*self->ways+location];

            if(self->replacement_policy_id == 0 || self->replacement_policy_id == 3) {
                // FIFO: nothing to do
                // RR: nothing to do
                placement_idx = self->ways-1;
                continue;
            } else { // if(self->replacement_policy_id == 1 || self->replacement_policy_id == 2) {
                // LRU: Reorder elements to account for access to element
                // MRU: Reorder elements to account for access to element
                if(location != 0) {
                    for(int j=location; j>0; j--) {
                        self->placement[set_id*self->ways+j] =
                            self->placement[set_id*self->ways+j-1];

                        // Reorder bitfild in accordance to queue
                        if(self->write_combining == 1) {
                            for(long i=0; i<self->subblock_bits; i++) {
                                if(BITTEST(self->subblock_bitfield,
                                           set_id*self->ways*self->subblock_bits +
                                           (j-1)*self->subblock_bits + i)) {
                                    BITSET(self->subblock_bitfield,
                                           set_id*self->ways*self->subblock_bits +
                                           j*self->subblock_bits + i);
                                } else {
                                    BITCLEAR(self->subblock_bitfield,
                                             set_id*self->ways*self->subblock_bits +
                                             j*self->subblock_bits + i);
                                }
                            }
                        }
                    }
                    self->placement[set_id*self->ways] = entry;
                }
                placement_idx = 0;
                continue;
            }
            // TODO if this is an exclusive cache, swap delivered cacheline with swap_cl_id (here and at end -> DO NOT RETURN)
        }

        // MISS!
        self->MISS.count++;
        // We only add actual bytes that were requested to miss.byte
        self->MISS.byte += self->cl_size < range.length ? self->cl_size : range.length;
#ifndef NO_PYTHON
        if(self->verbosity >= 2) {
            PySys_WriteStdout("%s CACHED [%li",
                              self->name, self->placement[set_id*self->ways].cl_id);
            for(long i=1; i<self->ways; i++) {
                PySys_WriteStdout(", %li", self->placement[set_id*self->ways+i].cl_id);
            }
            PySys_WriteStdout("]\n");
        }
        if(self->verbosity >= 1) {
            PySys_WriteStdout(
                "%s MISS self->LOAD=%lli addr=%li length=%li cl_id=%li set_id=%li\n",
                self->name, self->LOAD.count, range.addr, range.length, cl_id, set_id);
        }
#endif

        // Load from lower cachelevel
        // Check victim cache, if available
        int victim_hit = 0;
        if(self->victims_to != NULL) {
#ifndef NO_PYTHON
            Py_INCREF(self->victims_to);
#endif
            // check for hit in victim cache
            long victim_set_id = Cache__get_set_id((Cache*)self->victims_to, cl_id);
            int victim_location_victim = Cache__get_location((Cache*)self->victims_to, cl_id, victim_set_id);
            if(victim_location_victim != -1) {
                // hit in victim cache
#ifndef NO_PYTHON
                if(self->verbosity >= 1) {
                    PySys_WriteStdout("%s VICTIM HIT cl_id=%li\n", ((Cache*)self->victims_to)->name, cl_id);
                }
#endif
                // load data from victim cache
                Cache__load((Cache*)self->victims_to, Cache__get_range_from_cl_id(self, cl_id));
                // do NOT go onto load_from cache
                victim_hit = 1;
            } 
#ifndef NO_PYTHON
            else if(self->verbosity >= 1) {
                PySys_WriteStdout("%s VICTIM MISS cl_id=%li\n", ((Cache*)self->victims_to)->name, cl_id);
            }
            Py_DECREF(self->victims_to);
#endif
        }
        // If no hit in victim cache, or no victim cache available, go to next cache level
        if(!victim_hit && self->load_from != NULL) {
#ifndef NO_PYTHON
            Py_INCREF(self->load_from);
#endif
            // TODO use replace_entry to inform other cache of swap (in case of exclusive caches)
            Cache__load((Cache*)self->load_from, Cache__get_range_from_cl_id(self, cl_id));
            // TODO, replace_cl_id);
#ifndef NO_PYTHON
            Py_DECREF(self->load_from);
#endif
        } // else last-level-cache

        cache_entry entry;
        entry.cl_id = cl_id;
        entry.dirty = 0;
        entry.invalid = 0;

        // Inject new entry into own cache. This also handles replacement.
        placement_idx = Cache__inject(self, &entry);

        // TODO if this is an exclusive cache (swap_on_load = True), swap delivered cacheline with swap_cl_id (here and at hit)
    }
    // TODO Does this make sens or multiple cachelines? It is atm only used by write-allocate,
    // which should be fine, because requests are already split into individual cachelines
    return placement_idx;
}

void Cache__store(Cache* self, addr_range range, int non_temporal) {
    self->STORE.count++;
    self->STORE.byte += range.length;
    // Handle range:
    long last_cl_id = Cache__get_cacheline_id(self, range.addr+range.length-1);
    for(long cl_id=Cache__get_cacheline_id(self, range.addr); cl_id<=last_cl_id; cl_id++) {
        long set_id = Cache__get_set_id(self, cl_id);
        int location = Cache__get_location(self, cl_id, set_id);
#ifndef NO_PYTHON
        if(self->verbosity >= 2) {
            PySys_WriteStdout(
                "%s STORE=%lli NT=%i addr=%li length=%li cl_id=%li sets=%li location=%i\n",
                self->name, self->LOAD.count, non_temporal, range.addr, range.length,
                cl_id, self->sets, location);
        }
#endif

        if(self->write_allocate == 1 && non_temporal == 0) {
            // Write-allocate policy

            // Make sure line is loaded into cache (this will produce HITs and MISSes, iff it is 
            // not present in this cache):
            if(location == -1) {
                // TODO does this also make sens if store with write-allocate and MISS happens on L2?
                // or would this inject byte loads instead of CL loads into the statistic
                // TODO makes no sens if first level is write-through (all byte requests hit L2)
                location = Cache__load(self, Cache__get_range_from_cl_id(self, cl_id));
            }
        } else if(location == -1 && self->write_back == 1) {
            // In non-temporal store case, write-combining or write-through:
            // If the cacheline is not yet present, we inject a cachelien without loading it
            cache_entry entry;
            entry.cl_id = cl_id;
            entry.dirty = 1;
            entry.invalid = 0;
            location = Cache__inject(self, &entry);
        }

        // Mark address range as cached in the bitfield
        if(self->write_combining == 1) {
            // If write_combining is active, set the touched bits:
            // Extract local range
            long cl_start = Cache__get_addr_from_cl_id(self, cl_id);
            long start = range.addr > cl_start ? range.addr : cl_start;
            long end = range.addr+range.length < cl_start+self->cl_size ?
                                range.addr+range.length : cl_start+self->cl_size;
            // PySys_WriteStdout("cl_start=%i start=%i end=%i\n", cl_start, start, end);
            for(long i=start-cl_start; i<end-cl_start; i++) {
                BITSET(self->subblock_bitfield,
                       set_id*self->ways*self->subblock_bits + location*self->subblock_bits + i);
            }
        }

        if(self->write_back == 1 && location != -1) {
            // Write-back policy and cache-line in cache

            // Mark cacheline as dirty for later write-back during eviction
            self->placement[set_id*self->ways+location].dirty = 1;
            // PySys_WriteStdout("DIRTY\n");
        } else {
            // Write-through policy or cache-line not in cache

            // Store to lower cachelevel
            // TODO use Cache__inject
            if(self->store_to != NULL) {
                addr_range store_range = Cache__get_range_from_cl_id_and_range(self, cl_id, range);
                self->EVICT.count++;
                self->EVICT.byte += store_range.length;
#ifndef NO_PYTHON
                Py_INCREF(self->store_to);
#endif
                Cache__store((Cache*)(self->store_to),
                             store_range,
                             non_temporal);
#ifndef NO_PYTHON
                Py_DECREF(self->store_to);
#endif
            } // else last-level-cache
        }
    }

    // Print bitfield
#ifndef NO_PYTHON
    if(self->verbosity >= 3 && self->subblock_bitfield != NULL) {
        for(long k=0; k<self->sets; k++) {
           for(long j=0; j<self->ways; j++) {
                for(long i=0; i<self->subblock_bits; i++) {
                    if(BITTEST(self->subblock_bitfield,
                               k*self->subblock_bits*self->ways+self->subblock_bits*j+i)) {
                        PySys_WriteStdout("I");
                    } else {
                        PySys_WriteStdout("O");
                    }
                }
                PySys_WriteStdout("\n");
            }
            PySys_WriteStdout("\n\n");
        }
    }
#endif
}

#ifndef NO_PYTHON

static PyObject* Cache_load(Cache* self, PyObject *args, PyObject *kwds)
{
    addr_range range;
    range.length = 1; // default to 1

    static char *kwlist[] = {"addr", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I|I", kwlist, &range.addr, &range.length);

    Cache__load(self, range); // TODO , 0);
    // Swap cl_id is irrelevant here, since this is only called on first level cache

    Py_RETURN_NONE;
}

static PyObject* Cache_iterload(Cache* self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    addr_range range;
    range.length = 1; // default to 1

    static char *kwlist[] = {"addrs", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "O|I", kwlist, &addrs, &range.length);
    Py_INCREF(addrs);

    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    Py_DECREF(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }

    // Iterate of elements in addrs
    PyObject *addr;
    while((addr = PyIter_Next(addrs_iter))) {
        // For each address, build range:
#if PY_MAJOR_VERSION >= 3
        range.addr = PyLong_AsLong(addr);
#else
        range.addr = PyInt_AsLong(addr);
#endif
        Cache__load(self, range); // TODO , 0);
        // Swap cl_id is irrelevant here, since this is only called on first level cache

        Py_DECREF(addr);
    }
    Py_DECREF(addrs_iter);
    Py_RETURN_NONE;
}

static PyObject* Cache_store(Cache* self, PyObject *args, PyObject *kwds)
{
    addr_range range;
    range.length = 1; // default to 1

    static char *kwlist[] = {"addr", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I|I", kwlist, &range.addr, &range.length);

    // Handling ranges in c tremendously increases the speed for multiple elements
    Cache__store(self, range, 0);

    Py_RETURN_NONE;
}

static PyObject* Cache_iterstore(Cache* self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    addr_range range;
    range.length = 1; // default to 1

    static char *kwlist[] = {"addrs", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "O|I", kwlist, &addrs, &range.length);
    Py_INCREF(addrs);

    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    Py_DECREF(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }

    // Iterate of elements in addrs
    PyObject *addr;
    while((addr = PyIter_Next(addrs_iter))) {
        // For each address, build range:
#if PY_MAJOR_VERSION >= 3
        range.addr = PyLong_AsLong(addr);
#else
        range.addr = PyInt_AsLong(addr);
#endif
        Cache__store(self, range, 0);

        Py_DECREF(addr);
    }
    Py_DECREF(addrs_iter);
    Py_RETURN_NONE;
}

static PyObject* Cache_loadstore(Cache* self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    addr_range range;
    range.length = 1; // default to 1

    static char *kwlist[] = {"addrs", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "O|I", kwlist, &addrs, &range.length);
    Py_INCREF(addrs);

    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    Py_DECREF(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }

    // Iterate of elements in addrs
    PyObject *loadstore_item, *load_addrs, *store_addrs, *addr, *load_iter, *store_iter;
    while((loadstore_item = PyIter_Next(addrs_iter))) {
        if(!PySequence_Check(loadstore_item)) {
            PyErr_SetString(PyExc_ValueError, "addrs element does not provide a sequence protocol");
            return NULL;
        }
        // PySys_WriteStdout("LENGTH=%i\n", PySequence_Length(loadstore_item));
        if(PySequence_Length(loadstore_item) != 2){
            PyErr_SetString(PyExc_ValueError, "each addrs element needs exactly two elements");
            return NULL;
        }
        load_addrs = PySequence_GetItem(loadstore_item, 0);
        store_addrs = PySequence_GetItem(loadstore_item, 1);

        // Unless None (otherwise ignore loads)
        if(load_addrs != Py_None) {
            if(!PySequence_Check(load_addrs)) {
                PyErr_SetString(
                    PyExc_ValueError, "load element does not provide a sequence protocol");
                return NULL;
            }

            // FIXME maybe this is better: Pass along to iterstore
            //Cache_iterstore(self, store_addrs, length)

            // Iterate of elements in load_addrs
            load_iter = PyObject_GetIter(load_addrs);
            if(load_iter == NULL) {
                PyErr_SetString(PyExc_ValueError, "load address is not iteratable");
                return NULL;
            }
            while((addr = PyIter_Next(load_iter))) {
                // For each address, build range:
#if PY_MAJOR_VERSION >= 3
                range.addr = PyLong_AsLong(addr);
#else
                range.addr = PyInt_AsLong(addr);
#endif
                Cache__load(self, range); // TODO , 0);
                // Swap cl_id is irrelevant here, since this is only called on first level cache
                Py_DECREF(addr);
            }
            Py_DECREF(load_iter);
        }

        // If store addresses are given
        if(store_addrs != Py_None) {
            if(!PySequence_Check(store_addrs)) {
                PyErr_SetString(
                    PyExc_ValueError, "store element does not provide a sequence protocol");
                return NULL;
            }

            // FIXME maybe this is better: Pass along to iterstore
            //Cache_iterload(self, load_addrs, length)

            // Iterate of elements in store_addrs
            store_iter = PyObject_GetIter(store_addrs);
            if(store_iter == NULL) {
                PyErr_SetString(PyExc_ValueError, "store address is not iteratable");
                return NULL;
            }
            while((addr = PyIter_Next(store_iter))) {
                // For each address, build range:
#if PY_MAJOR_VERSION >= 3
                range.addr = PyLong_AsLong(addr);
#else
                range.addr = PyInt_AsLong(addr);
#endif
                Cache__store(self, range, 0);
                Py_DECREF(addr);
            }
            Py_DECREF(store_iter);
        }

        Py_DECREF(load_addrs);
        Py_DECREF(store_addrs);
        Py_DECREF(loadstore_item);
    }
    Py_DECREF(addrs_iter);
    Py_RETURN_NONE;
}

static PyObject* Cache_contains(Cache* self, PyObject *args, PyObject *kwds) {
    long addr;

    static char *kwlist[] = {"addr", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I", kwlist, &addr);

    long cl_id = Cache__get_cacheline_id(self, addr);
    long set_id = Cache__get_set_id(self, cl_id);

    for(long i=0; i<self->ways; i++) {
        if(self->placement[set_id*self->ways+i].invalid == 0 &&
           self->placement[set_id*self->ways+i].cl_id == cl_id) {
            Py_RETURN_TRUE;
        }
    }
    Py_RETURN_FALSE;
}

static PyObject* Cache_force_write_back(Cache* self) {
    // PySys_WriteStdout("%s force_write_back\n", self->name);
    for(long i=0; i<self->ways*self->sets; i++) {
        // PySys_WriteStdout("%i inv=%i dirty=%i\n", i, self->placement[i].invalid, self->placement[i].dirty);
        // TODO merge with Cache__inject (last section)?
        if(self->placement[i].invalid == 0 && self->placement[i].dirty == 1) {
            self->EVICT.count++;
            self->EVICT.byte += self->cl_size;
            if(self->verbosity >= 3) {
                PySys_WriteStdout(
                    "%s EVICT cl_id=%li invalid=%u dirty=%u\n",
                    self->name, self->placement[i].cl_id,
                    self->placement[i].invalid, self->placement[i].dirty);
            }
            if(self->store_to != NULL) {
                // Found dirty line, initiate write-back:
                // PySys_WriteStdout("%s dirty_line cl_id=%i write_combining=%i\n", self->name, self->placement[i].cl_id, self->write_combining);

                int non_temporal = 0; // default for non write-combining caches

                if(self->write_combining == 1) {
                    // Check if non-temporal store may be used or write-allocate is necessary
                    non_temporal = 1;
                    for(long j=0; j<self->subblock_bits; j++) {
                        if(!BITTEST(self->subblock_bitfield, i*self->subblock_bits + j)) {
                            // incomplete cacheline, thus write-allocate is necessary
                            non_temporal = 0;
                        }
                        // Clear bits for future use
                        BITCLEAR(self->subblock_bitfield, i*self->subblock_bits + j);
                    }
                }

                Py_INCREF(self->store_to);
                Cache__store(
                    (Cache*)self->store_to,
                    Cache__get_range_from_cl_id(self, self->placement[i].cl_id),
                    non_temporal);
                Py_DECREF(self->store_to);
            }
            self->placement[i].dirty = 0;
        }
    }
    Py_RETURN_NONE;
}

static PyObject* Cache_reset_stats(Cache* self) {
    self->LOAD.count = 0;
    self->STORE.count = 0;
    self->HIT.count = 0;
    self->MISS.count = 0;
    self->EVICT.count = 0;

    self->LOAD.byte = 0;
    self->STORE.byte = 0;
    self->HIT.byte = 0;
    self->MISS.byte = 0;
    self->EVICT.byte = 0;

    // self->LOAD.cl = 0;
    // self->STORE.cl = 0;
    // self->HIT.cl = 0;
    // self->MISS.cl = 0;

    Py_RETURN_NONE;
}

static PyObject* Cache_count_invalid_entries(Cache* self) {
    int count = 0;
    for(long i=0; i<self->ways*self->sets; i++) {
        if(self->placement[i].invalid == 1) {
            count++;
        }
    }
    return Py_BuildValue("i", count);
}

static PyObject* Cache_mark_all_invalid(Cache* self) {
    for(long i=0; i<self->ways*self->sets; i++) {
        self->placement[i].invalid = 1;
    }
    Py_RETURN_NONE;
}

static PyMethodDef Cache_methods[] = {
    {"load", (PyCFunction)Cache_load, METH_VARARGS|METH_KEYWORDS, NULL},
    {"iterload", (PyCFunction)Cache_iterload, METH_VARARGS|METH_KEYWORDS, NULL},
    {"store", (PyCFunction)Cache_store, METH_VARARGS|METH_KEYWORDS, NULL},
    {"iterstore", (PyCFunction)Cache_iterstore, METH_VARARGS|METH_KEYWORDS, NULL},
    {"loadstore", (PyCFunction)Cache_loadstore, METH_VARARGS|METH_KEYWORDS, NULL},
    {"contains", (PyCFunction)Cache_contains, METH_VARARGS, NULL},
    {"force_write_back", (PyCFunction)Cache_force_write_back, METH_VARARGS, NULL},
    {"reset_stats", (PyCFunction)Cache_reset_stats, METH_VARARGS, NULL},
    {"count_invalid_entries", (PyCFunction)Cache_count_invalid_entries, METH_VARARGS, NULL},
    {"mark_all_invalid", (PyCFunction)Cache_mark_all_invalid, METH_VARARGS, NULL},

    /* Sentinel */
    {NULL, NULL}
};

static PyObject* Cache_cached_get(Cache* self) {
    PyObject* cached_set = PySet_New(NULL);
    for(long i=0; i<self->sets*self->ways; i++) {
        //PySys_WriteStdout("i=%li cl_id=%li invalid=%u addr=%li\n", i, self->placement[i].cl_id, self->placement[i].invalid, Cache__get_addr_from_cl_id(self, self->placement[i].cl_id));
        // Skip invalidated entries
        if(self->placement[i].invalid) {
            continue;
        }

        // For each cached cacheline expand to all cached addresses:
        for(long j=0; j<self->cl_size; j++) {
            // PySys_WriteStdout("%li %li %li %li\n", self->sets, self->ways, i, self->placement[i].cl_id);
            PyObject* addr = PyLong_FromLong(
                Cache__get_addr_from_cl_id(self, self->placement[i].cl_id)+j);
            PySet_Add(cached_set, addr);
            Py_DECREF(addr);
        }
    }
    return cached_set;
}

static PyGetSetDef Cache_getset[] = {
    {"cached", (getter)Cache_cached_get, NULL, "cache", NULL},

    /* Sentinel */
    {NULL},
};

static int Cache_init(Cache *self, PyObject *args, PyObject *kwds);

static PyTypeObject CacheType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "cachesim.backend.Cache",  /* tp_name */
    sizeof(Cache),             /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)Cache_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "Cache objects",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Cache_methods,             /* tp_methods */
    Cache_members,             /* tp_members */
    Cache_getset,              /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Cache_init,      /* tp_init */
    0,                         /* tp_alloc */
    0,                         /* tp_new */
};

static int Cache_init(Cache *self, PyObject *args, PyObject *kwds) {
    PyObject *store_to, *load_from, *victims_to, *tmp;
    self->verbosity = 0;
    static char *kwlist[] = {"name", "sets", "ways", "cl_size",
                             "replacement_policy_id", "write_back", "write_allocate",
                             "write_combining", "subblock_size",
                             "load_from", "store_to", "victims_to",
                             "swap_on_load", "verbosity", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sIIIiiiiiOOOi|i", kwlist,
                                     &self->name, &self->sets, &self->ways, &self->cl_size,
                                     &self->replacement_policy_id,
                                     &self->write_back, &self->write_allocate,
                                     &self->write_combining, &self->subblock_size,
                                     &load_from, &store_to, &victims_to,
                                     &self->swap_on_load, &self->verbosity)) {
        return -1;
    }

    // Handle load_from parent (if given)
    if(load_from != Py_None) {
        if(!PyObject_IsInstance(load_from, (PyObject*)&CacheType)) {
            PyErr_SetString(PyExc_TypeError, "load_from needs to be backend.Cache or None");
            return -1;
        }
        tmp = self->load_from;
        Py_INCREF(load_from);
        self->load_from = load_from;
        Py_XDECREF(tmp);
    } else {
        self->load_from = NULL;
    }

    // Handle store_to parent (if given)
    if(store_to != Py_None) {
        if(!PyObject_IsInstance(store_to, (PyObject*)&CacheType)) {
            PyErr_SetString(PyExc_TypeError, "store_to needs to be backend.Cache or None");
            return -1;
        }
        tmp = self->store_to;
        Py_INCREF(store_to);
        self->store_to = store_to;
        Py_XDECREF(tmp);
    } else {
        self->store_to = NULL;
    }

    // Handle victims_to parent (if given)
    if(victims_to != Py_None) {
        if(!PyObject_IsInstance(victims_to, (PyObject*)&CacheType)) {
            PyErr_SetString(
                PyExc_TypeError, "victims_to needs to be of type backend.Cache or None");
            return -1;
        }
        tmp = self->victims_to;
        Py_INCREF(victims_to);
        self->victims_to = victims_to;
        Py_XDECREF(tmp);
    } else {
        self->victims_to = NULL;
    }

    // TODO validate store, load and victim paths so no null objects will be used until LLC/mem? is hit
    // should we introduce a memory object in c?

    self->placement = PyMem_New(struct cache_entry, self->sets*self->ways);
    for(long i=0; i<self->sets*self->ways; i++) {
        self->placement[i].invalid = 1;
        self->placement[i].dirty = 0;
    }

    // Check if cl_size is of power^2
    if(!isPowerOfTwo(self->cl_size)) {
        // throw exception
        PyErr_SetString(PyExc_ValueError, "cl_size needs to be a power of two.");
        return -1;
    }

    // Get number of bits in cacheline adressing
    self->cl_bits = log2_uint((unsigned long)self->cl_size);

    // Check if subblock_size is a divisor of cl_size
    if(self->cl_size % self->subblock_size != 0) {
        // throw exception
        PyErr_SetString(PyExc_ValueError, "subblock_size needs to be a devisor of cl_size.");
        return -1;
    }
    self->subblock_bits = self->cl_size/self->subblock_size;

    // Allocate subblock_bitfield
    if(self->write_combining && self->subblock_size != self->cl_size) {
        // Subblocking will be used:
        // since char is used as type, we need upper(subblock_bits/8) chars per placement
        self->subblock_bitfield = PyMem_New(
            char, BITNSLOTS(self->sets*self->ways*self->subblock_bits));
        // Clear all bits
        for(long i=0; i<BITNSLOTS(self->sets*self->ways*self->subblock_bits); i++) {
            BITCLEAR(self->subblock_bitfield, i);
        }
    } else {
        // Subblocking won't be used:
        self->subblock_bitfield = NULL;
    }

    Cache_reset_stats(self);

    if(self->verbosity >= 1) {
        PySys_WriteStdout("CACHE sets=%li ways=%li cl_size=%li cl_bits=%li\n",
                          self->sets, self->ways, self->cl_size, self->cl_bits);
    }

    return 0;
}


#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef moduledef = {
        PyModuleDef_HEAD_INIT,
        "cachesim.backend",
        "Backend of cachesim",
        -1,
        NULL, NULL, NULL, NULL, NULL
};

#define INITERROR return NULL

PyObject *
PyInit_backend(void)
#else
#define INITERROR return

void
initbackend(void)
#endif
{
#if PY_MAJOR_VERSION >= 3
    PyObject *module = PyModule_Create(&moduledef);
#else
    PyObject *module = Py_InitModule("cachesim.backend", cachesim_methods);
#endif

    if (module == NULL)
        INITERROR;

    CacheType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&CacheType) < 0)
        INITERROR;

    Py_INCREF(&CacheType);
    PyModule_AddObject(module, "Cache", (PyObject *)&CacheType);

#if PY_MAJOR_VERSION >= 3
    return module;
#endif
}

#endif

//file for outputlog when called from pintool (stdout not linkable)
FILE * file;

//! might break for more complicated cache hierarchies
void dealloc_cacheSim(Cache* cache)
{
    // fputs("dealloc cachesim\n",file);
    // fflush(file);
    //TODO free cache hierarchy, i load from != store_to != victims_to (breaks for skipping store_to or victims_to)
    //TODO prevent double free in case of circular cache references. deallocation not needed?
    if (cache->load_from != NULL)
        dealloc_cacheSim((Cache*)cache->load_from);
    free(cache->placement);
    free(cache);
    // fclose(file);
}

Cache* get_cacheSim_from_file(const char* cache_file)
{
    //file for log output and errors/warnings (needed because stdout and stderr for some reason cannot be linked when using pin)
    file  = fopen ("log_cachesim","w");
    fprintf(file, "Cache* get_cacheSim_from_file(\"%s\"):\n\n", cache_file);
    fflush(file);
    FILE* stream = fopen(cache_file, "r");

    char line[1024];

    int size = 0;
    //get number of required cache objects
    char* dummy = fgets(line, 1024, stream);
    if (dummy == NULL)
    {
        fprintf(file, "could not read from cache definition file\n");
        fflush(file);
        exit(EXIT_FAILURE);
    }
    size = atoi(line);

    if (size < 1)
    {
        fprintf(file, "invalid number of caches:%d\n", size);
        fflush(file);
        exit(EXIT_FAILURE);
    }

    //buffer for cache objects
    Cache* cacheSim[size];
    //buffers to save information about which caches to link at the end
    char* load_from_buff[size];
    memset(load_from_buff, 0, size*sizeof(char*));
    char* store_to_buff[size];
    memset(store_to_buff, 0, size*sizeof(char*));
    char* victims_to_buff[size];
    memset(victims_to_buff, 0, size*sizeof(char*));
    int linkcounter[size];
    memset(linkcounter, 0, size*sizeof(int));
    int counter = 0;

    char *token, *key, *value;
    char *saveptr1, *saveptr2;

    fputs("read input file\n",file);
    fflush(file);
    int linecounter = 1;
    while (fgets(line, 1024, stream) && counter != size)
    {
        ++linecounter;
        //read line, representing a cache
        if (line[0] != '\n' && line[0] != '\r' && line[0] != '#')
        {
            cacheSim[counter] = (Cache*) calloc(1, sizeof(Cache));
            if (&cacheSim[counter] == NULL)
            {
                fprintf(file, "allocation of memory for cache object failed\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }

            //key value pairs seperated by ','
            token = strtok_r(&line[0], ",\n\r", &saveptr1);
            while(token != NULL)
            {
                //key and value seperated by '='
                key = strtok_r(token, "=", &saveptr2);
                if (key == NULL)
                {
                    fprintf(file, "token without '=' in line %d\n", linecounter);
                    fflush(file);
                    continue;
                }
                value = strtok_r(NULL, "=\n\r", &saveptr2);
                if (value == NULL)
                {
                    fprintf(file, "token without value in line %d\n", linecounter);
                    fflush(file);
                    continue;
                }
                
                if (strcmp(key, "name") == 0)
                {
                    cacheSim[counter]->name = strdup(value);
                }
                else if (strcmp(key, "sets") == 0)
                {
                    cacheSim[counter]->sets = atoi(value);
                }
                else if (strcmp(key, "ways") == 0)
                {
                    cacheSim[counter]->ways = atoi(value);
                }
                else if (strcmp(key, "cl_size") == 0)
                {
                    cacheSim[counter]->cl_size = atoi(value);
                }
                else if (strcmp(key, "cl_bits") == 0)
                {
                    cacheSim[counter]->cl_bits = atoi(value);
                }
                else if (strcmp(key, "subblock_size") == 0)
                {
                    cacheSim[counter]->subblock_size = atoi(value);
                }
                else if (strcmp(key, "subblock_bits") == 0)
                {
                    cacheSim[counter]->subblock_bits = atoi(value);
                }
                else if (strcmp(key, "replacement_policy_id") == 0)
                {
                    cacheSim[counter]->replacement_policy_id = atoi(value);
                }
                else if (strcmp(key, "write_back") == 0)
                {
                    cacheSim[counter]->write_back = atoi(value);
                }
                else if (strcmp(key, "write_allocate") == 0)
                {
                    cacheSim[counter]->write_allocate = atoi(value);
                }
                else if (strcmp(key, "write_combining") == 0)
                {
                    cacheSim[counter]->write_combining = atoi(value);
                }
                else if (strcmp(key, "load_from") == 0)
                {
                    load_from_buff[counter] = strdup(value);
                }
                else if (strcmp(key, "store_to") == 0)
                {
                    store_to_buff[counter] = strdup(value);
                }
                else if (strcmp(key, "victims_to") == 0)
                {
                    victims_to_buff[counter] = strdup(value);
                }
                else if (strcmp(key, "swap_on_load") == 0)
                {
                    cacheSim[counter]->swap_on_load = atoi(value);
                }
                else
                {
                    fprintf(file, "unrecognized parameter:%s\n", key);
                    fflush(file);
                }

                token = strtok_r(NULL, ",", &saveptr1);
            }

            if(cacheSim[counter]->name == NULL)
            {
                fprintf(file, "cache with uninitialized name\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }
            if(cacheSim[counter]->sets == 0)
            {
                fprintf(file, "cache with uninitialized sets\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }
            if(cacheSim[counter]->ways == 0)
            {
                fprintf(file, "cache with uninitialized ways\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }
            if(cacheSim[counter]->cl_size == 0)
            {
                fprintf(file, "cache with uninitialized cl_size\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }
            if(cacheSim[counter]->subblock_size == 0)
            {
                cacheSim[counter]->subblock_size = cacheSim[counter]->cl_size;
            }
            //TODO more sanity checks?

            //Copy paste from pyinterface.c initialization:

            // Check if cl_size is of power^2
            if(!isPowerOfTwo(cacheSim[counter]->cl_size)) {
                fprintf(file, "cl_size is not a power of 2!\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }

            // Get number of bits in cacheline adressing
            cacheSim[counter]->cl_bits = log2_uint(cacheSim[counter]->cl_size);

            // Check if subblock_size is a divisor of cl_size
            if(cacheSim[counter]->cl_size % cacheSim[counter]->subblock_size != 0) {
                fprintf(file, "subblock_size needs to be a devisor of cl_size!\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }
            cacheSim[counter]->subblock_bits = cacheSim[counter]->cl_size/cacheSim[counter]->subblock_size;

            // Allocate subblock_bitfield
            if(cacheSim[counter]->write_combining && cacheSim[counter]->subblock_size != cacheSim[counter]->cl_size) {
                // Subblocking will be used:
                // since char is used as type, we need upper(subblock_bits/8) chars per placement
                cacheSim[counter]->subblock_bitfield = (char*) malloc(BITNSLOTS(cacheSim[counter]->sets*cacheSim[counter]->ways*cacheSim[counter]->subblock_bits) * sizeof(char));
                // Clear all bits
                for(int i=0; i<BITNSLOTS(cacheSim[counter]->sets*cacheSim[counter]->ways*cacheSim[counter]->subblock_bits); i++) {
                    BITCLEAR(cacheSim[counter]->subblock_bitfield, i);
                }
            } else {
                // Subblocking won't be used:
                cacheSim[counter]->subblock_bitfield = NULL;
            }

            //init cache
            cacheSim[counter]->placement = (cache_entry*) malloc(cacheSim[counter]->sets * cacheSim[counter]->ways * sizeof(cache_entry));
            if (cacheSim[counter]->placement == NULL)
            {
                fprintf(file, "allocation of memory for cache object failed\n");
                fflush(file);
                exit(EXIT_FAILURE);
            }
            for(unsigned int i=0; i<cacheSim[counter]->sets*cacheSim[counter]->ways; i++)
            {
                cacheSim[counter]->placement[i].invalid = 1;
                cacheSim[counter]->placement[i].dirty = 0;
            }

            ++counter;
        }

    }

    //link caches
    fputs("\nlink caches:\n",file);
    fflush(file);
    for (int i = 0; i < size; ++i)
    {
        // fprintf(file, "%d:\n  loadfrom: %s\n  storeto: %s\n  victimsto: %s\n\n",i, load_from_buff[i],store_to_buff[i],victims_to_buff[i]);
        // fflush(file);
        for (int j = 0; j < size; ++j)
        {
            if(cacheSim[j]->name != NULL){
                if (load_from_buff[i] != NULL && strcmp(load_from_buff[i], cacheSim[j]->name) == 0)
                {
                    cacheSim[i]->load_from = cacheSim[j];
                    ++linkcounter[j];
                }
                if (store_to_buff[i] != NULL && strcmp(store_to_buff[i], cacheSim[j]->name) == 0)
                {
                    cacheSim[i]->store_to = cacheSim[j];
                    ++linkcounter[j];
                }
                if (victims_to_buff[i] != NULL && strcmp(victims_to_buff[i], cacheSim[j]->name) == 0)
                {
                    cacheSim[i]->victims_to = cacheSim[j];
                    ++linkcounter[j];
                }
            }
        }
    }


    //find first level cache as interface for the cacheSimulator
    Cache* first_level = NULL;
    for (int i = 0; i < size; ++i)
    {
        // fprintf(file, "linkcounter %d: %d\n",i,linkcounter[i]);
        // fflush(file);
        if (linkcounter[i] == 0)
        {
            if (first_level != NULL)
            {
                fputs("cache that is not first level has no connection! exiting!\n\n",file);
                fflush(file);
                exit(EXIT_FAILURE);
            }
            first_level = cacheSim[i];
        }
    }
    if (first_level == NULL)
    {
        fputs("first level is null! exiting!\n\n",file);
        fflush(file);
        exit(EXIT_FAILURE);
    }

    fputs("done\n",file);
    fflush(file);

    fputs("\nfreeing resources:\n",file);
    fflush(file);
    //close file and free stuff
    fclose(stream);
    
    for (int i = 0; i < size; ++i)
    {
        free(load_from_buff[i]);
        free(store_to_buff[i]);
        free(victims_to_buff[i]);
    }

    fputs("done\n\nreturning cache...\n",file);
    fclose(file);
    return first_level;
}

//has to be left out when using pin, as stdout for some reason cannot be linked in this case
#ifndef USE_PIN
void printStats(Cache* cache)
{
    fprintf(stdout, "%s:\n",cache->name);
    fprintf(stdout, "LOAD: %llu   size: %lluB\n",cache->LOAD.count, cache->LOAD.byte);
    fprintf(stdout, "STORE: %llu   size: %lluB\n",cache->STORE.count, cache->STORE.byte);
    fprintf(stdout, "HIT: %llu   size: %lluB\n",cache->HIT.count, cache->HIT.byte);
    fprintf(stdout, "MISS: %llu   size: %lluB\n",cache->MISS.count, cache->MISS.byte);
    fprintf(stdout, "EVICT: %llu   size: %lluB\n",cache->EVICT.count, cache->EVICT.byte);

    if (cache->load_from != NULL)
        printStats(cache->load_from);
    if (cache->store_to != NULL && cache->store_to != cache->load_from)
        printStats(cache->store_to);
    if (cache->victims_to != NULL && cache->store_to != cache->load_from && cache->store_to != cache->victims_to)
        printStats(cache->victims_to);
}
#endif
