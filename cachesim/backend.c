#include "Python.h"
#include <structmember.h>
#include <limits.h>

// Array of bits as found in comp.lang.c FAQ Question 20.8: http://c-faq.com/misc/bitsets.html
#define BITMASK(b) (1 << ((b) % CHAR_BIT))
#define BITSLOT(b) ((b) / CHAR_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + CHAR_BIT - 1) / CHAR_BIT)

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

typedef struct cache_entry {
    unsigned int cl_id;

    unsigned int dirty : 1; // if 0, content is in sync with main memory. if 1, it is not.
                            // used for write-back
    unsigned int invalid : 1; // denotes an entry which does not contain a valid cacheline.
                              // it is empty.
} cache_entry;

typedef struct addr_range {
    // Address range used to communicate consecutive accesses
    // last addr of range is addr+length-1
    unsigned int addr;
    unsigned int length;
} addr_range;

struct stats {
    unsigned int count;
    unsigned int byte;
    //unsigned int cl; // might be used later
};

typedef struct Cache {
    PyObject_HEAD
    const char *name;
    unsigned int sets;
    unsigned int ways;
    unsigned int cl_size;
    unsigned int cl_bits;
    unsigned int subblock_size;
    unsigned int subblock_bits;
    int replacement_policy_id; // 0 = FIFO, 1 = LRU, 2 = MRU, 3 = RR
                               // (state is kept in the ordering)
                               // for LFU an additional field would be required to capture state
    int write_back; // 1 = write-back
                    // 0 = write-through
    int write_allocate; // 1 = write-allocate,
                        // 0 = non-write-allocate
    int write_combining; // 1 = this is a write-combining cache
                         // 0 = regular cache

    PyObject *load_from;
    PyObject *store_to;
    PyObject *victims_to;
    int swap_on_load;

    cache_entry *placement;
    char *subblock_bitfield;

    struct stats LOAD;
    struct stats STORE;
    struct stats HIT;
    struct stats MISS;
    struct stats EVICT;

    int verbosity;
} Cache;

static void Cache_dealloc(Cache* self) {
    Py_XDECREF(self->store_to);
    Py_XDECREF(self->load_from);
    PyMem_Del(self->placement);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

unsigned int log2_uint(unsigned int x) {
    unsigned int ans = 0;
    while(x >>= 1) {
        ans++;
    }
    return ans;
}

int isPowerOfTwo(unsigned int x) {
    return ((x != 0) && !(x & (x - 1)));
}

static PyMemberDef Cache_members[] = {
    {"name", T_STRING, offsetof(Cache, name), 0,
     "name of cache level"},
    {"sets", T_UINT, offsetof(Cache, sets), 0,
     "number of sets available"},
    {"ways", T_UINT, offsetof(Cache, ways), 0,
     "number of ways available"},
    {"cl_size", T_UINT, offsetof(Cache, cl_size), 0,
     "number of bytes in a cacheline"},
    {"cl_bits", T_UINT, offsetof(Cache, cl_bits), 0,
     "number of bits used to identiy individual bytes in a cacheline"},
    {"subblock_size", T_UINT, offsetof(Cache, subblock_size), 0,
     "number of bytes per subblock (must be a devisor of cl_size)"},
    {"subblock_bits", T_UINT, offsetof(Cache, subblock_bits), 0,
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
    {"LOAD_count", T_UINT, offsetof(Cache, LOAD.count), 0,
     "number of loads performed"},
    {"LOAD_byte", T_UINT, offsetof(Cache, LOAD.byte), 0,
     "number of bytes loaded"},
    {"STORE_count", T_UINT, offsetof(Cache, STORE.count), 0,
     "number of stores performed"},
    {"STORE_byte", T_UINT, offsetof(Cache, STORE.byte), 0,
     "number of bytes stored"},
    {"HIT_count", T_UINT, offsetof(Cache, HIT.count), 0,
     "number of cache hits"},
    {"HIT_byte", T_UINT, offsetof(Cache, HIT.byte), 0,
     "number of bytes that were cache hits"},
    {"MISS_count", T_UINT, offsetof(Cache, MISS.count), 0,
     "number of misses"},
    {"MISS_byte", T_UINT, offsetof(Cache, MISS.byte), 0,
     "number of bytes missed"},
    {"EVICT_count", T_UINT, offsetof(Cache, EVICT.count), 0,
     "number of evicts"},
    {"EVICT_byte", T_UINT, offsetof(Cache, EVICT.byte), 0,
     "number of bytes evicted"},
    {"verbosity", T_INT, offsetof(Cache, verbosity), 0,
     "verbosity level of output"},
    {NULL}  /* Sentinel */
};

inline static unsigned int Cache__get_cacheline_id(Cache* self, unsigned int addr) {
    return addr >> self->cl_bits;
}

inline static unsigned int Cache__get_set_id(Cache* self, unsigned int cl_id) {
    return cl_id % self->sets;
}

inline static addr_range __range_from_addrs(unsigned int addr, unsigned int last_addr) {
    addr_range range;
    range.addr = addr;
    range.length = last_addr-addr-1;
    return range;
}

inline static unsigned int Cache__get_addr_from_cl_id(Cache* self, unsigned int cl_id) {
    return cl_id << self->cl_bits;
}

inline static addr_range Cache__get_range_from_cl_id(Cache* self, unsigned int cl_id) {
    addr_range range;
    range.addr = Cache__get_addr_from_cl_id(self, cl_id);
    range.length = self->cl_size;
    return range;
}

inline static addr_range Cache__get_range_from_cl_id_and_range(
        Cache* self, unsigned int cl_id, addr_range range) {
    // Creates a range from a cacheline id and another range.
    // The returned range will always be a subset of (or at most the same as) the given range
    addr_range new_range;
    new_range.addr = Cache__get_addr_from_cl_id(self, cl_id);
    new_range.addr = new_range.addr > range.addr ? new_range.addr : range.addr;
    new_range.length = new_range.addr+self->cl_size < range.addr+range.length ?
        self->cl_size : range.addr+range.length-new_range.addr;
    return new_range;
}

inline static int Cache__get_location(Cache* self, unsigned int cl_id, unsigned int set_id) {
    // Returns the location a cacheline has in a cache
    // if cacheline is not present, returns -1
    // TODO use sorted data structure for faster searches in case of large number of
    // ways or full-associativity?

    for(int i=0; i<self->ways; i++) {
        if(self->placement[set_id*self->ways+i].invalid == 0 &&
           self->placement[set_id*self->ways+i].cl_id == cl_id) {
            return i;
        }
    }

    return -1; // Not found
}

static void Cache__store(Cache* self, addr_range range, int non_temporal);

static int Cache__inject(Cache* self, cache_entry* entry) {
    /*
    Injects a cache entry into a cache and handles all side effects that might occur:
     - choose replacement according to policy
     - reorder queues
     - inform victim caches
     - handle write-back on replacement
    */
    unsigned int set_id = Cache__get_set_id(self, entry->cl_id);

    // Get cacheline id to be replaced according to replacement strategy
    int replace_idx;
    cache_entry replace_entry;

    if(self->replacement_policy_id == 0 || self->replacement_policy_id == 1) {
        // FIFO: replace end of queue
        // LRU: replace end of queue
        replace_idx = 0;
        replace_entry = self->placement[set_id*self->ways+self->ways-1];

        // Reorder queue
        for(int i=self->ways-1; i>0; i--) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i-1];

            // Reorder bitfild in accordance to queue
            if(self->write_combining == 1) {
                for(int j=0; j<self->subblock_bits; j++) {
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
        for(int i=0; i>self->ways-1; i++) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i+1];

            // Reorder bitfild in accordance to queue
            if(self->write_combining == 1) {
                for(int j=0; j<self->subblock_bits; j++) {
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

    if(self->verbosity >= 3) {
        PySys_WriteStdout(
            "%s REPLACED cl_id=%i invalid=%i dirty=%i\n",
            self->name, replace_entry.cl_id, replace_entry.invalid, replace_entry.dirty);
    }

    // write-back: check for dirty bit of replaced and inform next lower level of store
    if(self->write_back == 1) {
        if(replace_entry.invalid == 0 && replace_entry.dirty == 1) {
            self->EVICT.count++;
            self->EVICT.byte += self->cl_size;
            if(self->verbosity >= 3) {
                PySys_WriteStdout(
                    "%s EVICT cl_id=%i\n",
                    self->name, replace_entry.cl_id, replace_entry.invalid, replace_entry.dirty);
            }
            if(self->store_to != NULL) {
                int non_temporal = 0; // default for non write-combining caches

                if(self->write_combining == 1) {
                    // Check if non-temporal store may be used or write-allocate is necessary
                    non_temporal = 1;
                    for(int i=0; i<self->subblock_bits; i++) {
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

                Py_INCREF(self->store_to);
                // TODO addrs vs cl_id is not nicely solved here
                Cache__store(
                    (Cache*)self->store_to,
                    Cache__get_range_from_cl_id(self, replace_entry.cl_id),
                    non_temporal);
                Py_DECREF(self->store_to);
            } // else last-level-cache
        }
    }
    // Deliver replaced cacheline to victim cache, if configured, valid and neither dirty or non-write_back
    // (if it were dirty, it would have been written to store_to if write_back is enabled)
    if(self->victims_to != NULL && replace_entry.invalid == 0 && (replace_entry.dirty != 1 || self->write_back == 0)) {
        Py_INCREF(self->victims_to);
        // Inject into victims_to
        Cache__inject((Cache*)self->victims_to, &replace_entry);
        Py_DECREF(self->victims_to);
        // Take care to include into evict stats
        self->EVICT.count++;
        self->EVICT.byte += self->cl_size;
    }

    return replace_idx;
}

static int Cache__load(Cache* self, addr_range range) {
    /*
    Signals request of addr range by higher level. This handles hits and misses.
    */
    self->LOAD.count++;
    self->LOAD.byte += range.length;
    int placement_idx = -1;

    // Handle range:
    unsigned int last_cl_id = Cache__get_cacheline_id(self, range.addr+range.length-1);
    for(unsigned int cl_id=Cache__get_cacheline_id(self, range.addr); cl_id<=last_cl_id; cl_id++) {
        unsigned int set_id = Cache__get_set_id(self, cl_id);
        if(self->verbosity >= 4) {
            PySys_WriteStdout(
                "%s LOAD=%i addr=%i length=%i cl_id=%i set_id=%i\n",
                self->name, self->LOAD.count, range.addr, range.length, cl_id, set_id);
        }

        // Check if cl_id is already cached
        int location = Cache__get_location(self, cl_id, set_id);
        if(location != -1) {
            // HIT: Found it!
            self->HIT.count++;
            // We only add actual bytes that were requested to hit.byte
            self->HIT.byte += self->cl_size < range.length ? self->cl_size : range.length;
            if(self->verbosity >= 3) {
                PySys_WriteStdout("%s HIT self->LOAD=%i addr=%i cl_id=%i set_id=%i\n",
                                  self->name, self->LOAD.count, range.addr, cl_id, set_id);
            }

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
                            for(int i=0; i<self->subblock_bits; i++) {
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
        if(self->verbosity >= 2) {
            PySys_WriteStdout("%s CACHED [%i",
                              self->name, self->placement[set_id*self->ways].cl_id);
            for(int i=1; i<self->ways; i++) {
                PySys_WriteStdout(", %i", self->placement[set_id*self->ways+i].cl_id);
            }
            PySys_WriteStdout("]\n");
        }
        if(self->verbosity >= 1) {
            PySys_WriteStdout(
                "%s MISS self->LOAD=%i addr=%i length=%i cl_id=%i set_id=%i\n",
                self->name, self->LOAD.count, range.addr, range.length, cl_id, set_id);
        }

        // Load from lower cachelevel
        // Check victim cache, if available
        int victim_hit = 0;
        if(self->victims_to != NULL) {
            Py_INCREF(self->victims_to);
            // check for hit in victim cache
            unsigned int victim_set_id = Cache__get_set_id((Cache*)self->victims_to, cl_id);
            int victim_location_victim = Cache__get_location((Cache*)self->victims_to, cl_id, victim_set_id);
            if(victim_location_victim != -1) {
                // hit in victim cache
                if(self->verbosity >= 1) {
                    PySys_WriteStdout("%s VICTIM HIT cl_id=%i\n", ((Cache*)self->victims_to)->name, cl_id);
                }
                // load data from victim cache
                Cache__load((Cache*)self->victims_to, Cache__get_range_from_cl_id(self, cl_id));
                // do NOT go onto load_from cache
                victim_hit = 1;
            } else if(self->verbosity >= 1) {
                PySys_WriteStdout("%s VICTIM MISS cl_id=%i\n", ((Cache*)self->victims_to)->name, cl_id);
            }
            Py_DECREF(self->victims_to);
        }
        // If no hit in victim cache, or no victim cache available, go to next cache level
        if(!victim_hit && self->load_from != NULL) {
            Py_INCREF(self->load_from);
            // TODO use replace_entry to inform other cache of swap (in case of exclusive caches)
            Cache__load((Cache*)self->load_from, Cache__get_range_from_cl_id(self, cl_id));
            // TODO, replace_cl_id);
            Py_DECREF(self->load_from);
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

static void Cache__store(Cache* self, addr_range range, int non_temporal) {
    self->STORE.count++;
    self->STORE.byte += range.length;
    // Handle range:
    unsigned int last_cl_id = Cache__get_cacheline_id(self, range.addr+range.length-1);
    for(unsigned int cl_id=Cache__get_cacheline_id(self, range.addr); cl_id<=last_cl_id; cl_id++) {
        unsigned int set_id = Cache__get_set_id(self, cl_id);
        int location = Cache__get_location(self, cl_id, set_id);
        if(self->verbosity >= 2) {
            PySys_WriteStdout("%s STORE=%i NT=%i addr=%i length=%i cl_id=%i sets=%i location=%i\n",
                              self->name, self->LOAD.count, non_temporal, range.addr, range.length,
                              cl_id, self->sets, location);
        }

        if(self->write_allocate == 1 && non_temporal == 0) {
            // Write-allocate policy

            // Make sure line is loaded into cache (this will produce HITs and MISSes):
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
            int cl_start = Cache__get_addr_from_cl_id(self, cl_id);
            int start = range.addr > cl_start ? range.addr : cl_start;
            int end = range.addr+range.length < cl_start+self->cl_size ?
                      range.addr+range.length : cl_start+self->cl_size;
            // PySys_WriteStdout("cl_start=%i start=%i end=%i\n", cl_start, start, end);
            for(int i=start-cl_start; i<end-cl_start; i++) {
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
                Py_INCREF(self->store_to);
                Cache__store((Cache*)(self->store_to),
                             store_range,
                             non_temporal);
                Py_DECREF(self->store_to);
            } // else last-level-cache
        }
    }

    // Print bitfield
    if(self->verbosity >= 3 && self->subblock_bitfield != NULL) {
        for(int k=0; k<self->sets; k++) {
           for(int j=0; j<self->ways; j++) {
                for(int i=0; i<self->subblock_bits; i++) {
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
}

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

    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }

    // Iterate of elements in addrs
    PyObject *addr;
    while((addr = PyIter_Next(addrs_iter))) {
        // For each address, build range:
#if PY_MAJOR_VERSION >= 3
        range.addr = PyLong_AsUnsignedLongMask(addr);
#else
        range.addr = PyInt_AsUnsignedLongMask(addr);
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

    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }

    // Iterate of elements in addrs
    PyObject *addr;
    while((addr = PyIter_Next(addrs_iter))) {
        // For each address, build range:
#if PY_MAJOR_VERSION >= 3
        range.addr = PyLong_AsUnsignedLongMask(addr);
#else
        range.addr = PyInt_AsUnsignedLongMask(addr);
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

    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
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
                range.addr = PyLong_AsUnsignedLongMask(addr);
#else
                range.addr = PyInt_AsUnsignedLongMask(addr);
#endif
                Cache__load(self, range); // TODO , 0);
                // Swap cl_id is irrelevant here, since this is only called on first level cache
                Py_DECREF(addr);
            }
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
                range.addr = PyLong_AsUnsignedLongMask(addr);
#else
                range.addr = PyInt_AsUnsignedLongMask(addr);
#endif
                Cache__store(self, range, 0);
                Py_DECREF(addr);
            }
        }

        Py_DECREF(load_addrs);
        Py_DECREF(store_addrs);
        Py_DECREF(loadstore_item);
    }
    Py_DECREF(addrs_iter);
    Py_RETURN_NONE;
}

static PyObject* Cache_contains(Cache* self, PyObject *args, PyObject *kwds) {
    unsigned int addr;

    static char *kwlist[] = {"addr", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I", kwlist, &addr);

    unsigned int cl_id = Cache__get_cacheline_id(self, addr);
    unsigned int set_id = Cache__get_set_id(self, cl_id);

    for(int i=0; i<self->ways; i++) {
        if(self->placement[set_id*self->ways+i].invalid == 0 &&
           self->placement[set_id*self->ways+i].cl_id == cl_id) {
            Py_RETURN_TRUE;
        }
    }
    Py_RETURN_FALSE;
}

static PyObject* Cache_force_write_back(Cache* self) {
    // PySys_WriteStdout("%s force_write_back\n", self->name);
    for(int i=0; i<self->ways*self->sets; i++) {
        // PySys_WriteStdout("%i inv=%i dirty=%i\n", i, self->placement[i].invalid, self->placement[i].dirty);
        // TODO merge with Cache__inject (last section)?
        if(self->placement[i].invalid == 0 && self->placement[i].dirty == 1) {
            self->EVICT.count++;
            self->EVICT.byte += self->cl_size;
            if(self->verbosity >= 3) {
                PySys_WriteStdout(
                    "%s EVICT cl_id=%i\n",
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
                    for(int j=0; j<self->subblock_bits; j++) {
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

static PyMethodDef Cache_methods[] = {
    {"load", (PyCFunction)Cache_load, METH_VARARGS|METH_KEYWORDS, NULL},
    {"iterload", (PyCFunction)Cache_iterload, METH_VARARGS|METH_KEYWORDS, NULL},
    {"store", (PyCFunction)Cache_store, METH_VARARGS|METH_KEYWORDS, NULL},
    {"iterstore", (PyCFunction)Cache_iterstore, METH_VARARGS|METH_KEYWORDS, NULL},
    {"loadstore", (PyCFunction)Cache_loadstore, METH_VARARGS|METH_KEYWORDS, NULL},
    {"contains", (PyCFunction)Cache_contains, METH_VARARGS, NULL},
    {"force_write_back", (PyCFunction)Cache_force_write_back, METH_VARARGS, NULL},
    {"reset_stats", (PyCFunction)Cache_reset_stats, METH_VARARGS, NULL},

    /* Sentinel */
    {NULL, NULL}
};

static PyObject* Cache_cached_get(Cache* self) {
    PyObject* cached_set = PySet_New(NULL);
    for(int i=0; i<self->sets*self->ways; i++) {
        //PySys_WriteStdout("i=%i cl_id=%i invalid=%i addr=%i\n", i, self->placement[i].cl_id, self->placement[i].invalid, Cache__get_addr_from_cl_id(self, self->placement[i].cl_id));
        // Skip invalidated entries
        if(self->placement[i].invalid) {
            continue;
        }

        // For each cached cacheline expand to all cached addresses:
        for(int j=0; j<self->cl_size; j++) {
            // PySys_WriteStdout("%i %i %i %i\n", self->sets, self->ways, i, self->placement[i].cl_id);
            PyObject* addr = PyLong_FromUnsignedLong(
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
    for(unsigned int i=0; i<self->sets*self->ways; i++) {
        self->placement[i].invalid = 1;
        self->placement[i].dirty = 0;
    }

    // Check if ways and cl_size are of power^2
    if(!isPowerOfTwo(self->ways)) {
        // throw exception
        PyErr_SetString(PyExc_ValueError, "ways needs to be a power of two.");
        return -1;
    }
    if(!isPowerOfTwo(self->cl_size)) {
        // throw exception
        PyErr_SetString(PyExc_ValueError, "cl_size needs to be a power of two.");
        return -1;
    }

    // Get number of bits in cacheline adressing
    self->cl_bits = log2_uint(self->cl_size);

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
        for(int i=0; i<BITNSLOTS(self->sets*self->ways*self->subblock_bits); i++) {
            BITCLEAR(self->subblock_bitfield, i);
        }
    } else {
        // Subblocking won't be used:
        self->subblock_bitfield = NULL;
    }

    Cache_reset_stats(self);

    if(self->verbosity >= 1) {
        PySys_WriteStdout("CACHE sets=%i ways=%i cl_size=%i cl_bits=%i\n",
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