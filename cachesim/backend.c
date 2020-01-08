#ifndef NO_PYTHON
#include "Python.h"
#include <structmember.h>
#endif

#include <stdlib.h>
#include <limits.h>
#include "backend.h"

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

void Cache__store(Cache* self, addr_range range, int non_temporal);

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
#ifndef NO_PYTHON
    if(self->verbosity >= 3) {
        PySys_WriteStdout(
            "%s REPLACED cl_id=%i invalid=%i dirty=%i\n",
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
                    "%s EVICT cl_id=%i\n",
                    self->name, replace_entry.cl_id, replace_entry.invalid, replace_entry.dirty);
            }
#endif
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
            Cache* victims_to = self->victims_to;
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
    unsigned int last_cl_id = Cache__get_cacheline_id(self, range.addr+range.length-1);
    for(unsigned int cl_id=Cache__get_cacheline_id(self, range.addr); cl_id<=last_cl_id; cl_id++) {
        unsigned int set_id = Cache__get_set_id(self, cl_id);
#ifndef NO_PYTHON
        if(self->verbosity >= 4) {
            PySys_WriteStdout(
                "%s LOAD=%i addr=%i length=%i cl_id=%i set_id=%i\n",
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
                PySys_WriteStdout("%s HIT self->LOAD=%i addr=%i cl_id=%i set_id=%i\n",
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
#ifndef NO_PYTHON
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
#endif

        // Load from lower cachelevel
        // Check victim cache, if available
        int victim_hit = 0;
        if(self->victims_to != NULL) {
#ifndef NO_PYTHON
            Py_INCREF(self->victims_to);
#endif
            // check for hit in victim cache
            unsigned int victim_set_id = Cache__get_set_id((Cache*)self->victims_to, cl_id);
            int victim_location_victim = Cache__get_location((Cache*)self->victims_to, cl_id, victim_set_id);
            if(victim_location_victim != -1) {
                // hit in victim cache
#ifndef NO_PYTHON
                if(self->verbosity >= 1) {
                    PySys_WriteStdout("%s VICTIM HIT cl_id=%i\n", ((Cache*)self->victims_to)->name, cl_id);
                }
#endif
                // load data from victim cache
                Cache__load((Cache*)self->victims_to, Cache__get_range_from_cl_id(self, cl_id));
                // do NOT go onto load_from cache
                victim_hit = 1;
            }
#ifndef NO_PYTHON
            else if(self->verbosity >= 1) {
                PySys_WriteStdout("%s VICTIM MISS cl_id=%i\n", ((Cache*)self->victims_to)->name, cl_id);
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
    unsigned int last_cl_id = Cache__get_cacheline_id(self, range.addr+range.length-1);
    for(unsigned int cl_id=Cache__get_cacheline_id(self, range.addr); cl_id<=last_cl_id; cl_id++) {
        unsigned int set_id = Cache__get_set_id(self, cl_id);
        int location = Cache__get_location(self, cl_id, set_id);
#ifndef NO_PYTHON
        if(self->verbosity >= 2) {
            PySys_WriteStdout("%s STORE=%i NT=%i addr=%i length=%i cl_id=%i sets=%i location=%i\n",
                            self->name, self->LOAD.count, non_temporal, range.addr, range.length,
                            cl_id, self->sets, location);
        }
#endif

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
#endif
}
