#include "Python.h"
#include <structmember.h>

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

    // TODO Use binary flags instead?
    int dirty; // if 0, content is in sync with main memory. if 1, it is not. used for write-back
    int invalid; // denotes an entry which does not contain a valid cacheline. it is empty.
} cache_entry;

typedef struct Cache {
    PyObject_HEAD
    const char *name;
    unsigned int sets;
    unsigned int set_bits;
    unsigned int ways;
    unsigned int cl_size;
    unsigned int cl_bits;
    int replacement_policy_id; // 0 = FIFO, 1 = LRU, 2 = MRU, 3 = RR 
                               // (state is kept in the ordering)
                               // for LFU an additional field would be required to capture state
    int write_back;            // 0 = write-back and write-allocate,
                               // 1 = write-through and non-write-allocate
    int write_allocate;        // 1 = write-allocate,
                               // 0 = non-write-allocate

    PyObject *load_from;
    PyObject *store_to;
    PyObject *victims_to;
    int swap_on_load;
    
    cache_entry *placement;
    unsigned int LOAD;
    unsigned int STORE;
    unsigned int HIT;
    unsigned int MISS;
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

static PyMemberDef Cache_members[] = {
    {"sets", T_UINT, offsetof(Cache, sets), 0,
     "number of sets available"},
    {"ways", T_UINT, offsetof(Cache, ways), 0,
     "number of ways available"},
    {"cl_size", T_UINT, offsetof(Cache, cl_size), 0,
     "number of bytes in a cacheline"},
    {"cl_bits", T_UINT, offsetof(Cache, cl_bits), 0,
     "number of bits used to identiy individual bytes in a cacheline"},
    {"replacement_policy_id", T_INT, offsetof(Cache, replacement_policy_id), 0,
     "replacement strategy of cachlevel"},
    {"write_back", T_INT, offsetof(Cache, write_back), 0,
     "write back of cachlevel (0 is write-through, 1 is write-back)"},
    {"write_allocate", T_INT, offsetof(Cache, write_allocate), 0,
     "write allocate of cachlevel (0 is non-write-allocate, 1 is write-allocate)"},
    {"write_combining", T_INT, offsetof(Cache, write_combining), 0,
     "combine writes on this level, before passing them on"},
    {"load_from", T_OBJECT_EX, offsetof(Cache, load_from), 0,
     "load parent Cache object (cache level which is closer to main memory)"},
    {"store_to", T_OBJECT_EX, offsetof(Cache, store_to), 0,
     "store parent Cache object (cache level which is closer to main memory)"},
    {"victims_to", T_OBJECT_EX, offsetof(Cache, victims_to), 0,
     "Cache object where victims will be send to (closer to main memory, None if victims vanish)"},
    {"LOAD", T_UINT, offsetof(Cache, LOAD), 0,
     "number of loads performed since last counter reset"},
    {"STORE", T_UINT, offsetof(Cache, STORE), 0,
     "number of stores performed since last counter reset"},
    {"HIT", T_UINT, offsetof(Cache, HIT), 0,
     "number of cache hits since last counter reset"},
    {"MISS", T_UINT, offsetof(Cache, MISS), 0,
     "number of misses since last counter reset"},
    {NULL}  /* Sentinel */
};

inline static unsigned int Cache__get_cacheline_id(Cache* self, unsigned int addr) {
    return addr >> self->cl_bits;
}

inline static unsigned int Cache__get_set_id(Cache* self, unsigned int cl_id) {
    return cl_id % self->sets;
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

static void Cache__store(Cache* self, unsigned int addr);

static int Cache__inject(Cache* self, cache_entry* entry) {
    /*
    Injects a cache entry into a cache and handles all sideeffects that might occure:
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
        }
    } else if(self->replacement_policy_id == 2) {
        // MRU: replace first of queue
        replace_idx = self->ways-1;
        replace_entry = self->placement[set_id*self->ways];
        
        // Reorder queue
        for(int i=0; i>self->ways-1; i++) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i+1];
        }
    } else { // if(self->replacement_policy_id == 3) {
        // RR: replace random element
        replace_idx = rand() & (self->ways - 1);
        replace_entry = self->placement[set_id*self->ways+replace_idx];
    }
    
    // Replace other cacheline according to replacement strategy (using placement order as state)
    self->placement[set_id*self->ways+replace_idx] = *entry;
    
    // write-back: check for dirty bit of replaced and inform next lower level of store
    if(self->write_back == 1) {
        if(replace_entry.invalid == 0 && replace_entry.dirty == 1) {
            if(self->store_to != NULL) {
                Py_INCREF(self->store_to);
                // TODO addrs vs cl_id is not nicely solved here
                Cache__store((Cache*)self->store_to, replace_entry.cl_id*self->cl_size);
                Py_DECREF(self->store_to);
            } // else last-level-cache
        }
    }
    
    // Deliver replaced cacheline to victim cache, if configured and valid
    if(self->victims_to != NULL && replace_entry.invalid == 0) {
        // Inject into victims_to
        Cache__inject((Cache*)self->victims_to, &replace_entry);
    }
    
    return replace_idx;
}

static int Cache__load(Cache* self, unsigned int addr) {
    /*
    Signals request of addr by higher level. This handles hits and misses.
    */
    
    self->LOAD++;
    unsigned int cl_id = Cache__get_cacheline_id(self, addr);
    unsigned int set_id = Cache__get_set_id(self, cl_id);
    //if(self->ways == 8) { //&& self->LOAD < 200) {
        // PySys_WriteStdout("LOAD=%i addr=%i cl_id=%i set_id=%i\n", self->LOAD, addr, cl_id, set_id);
    //}

    // Check if cl_id is already cached
    int location = Cache__get_location(self, cl_id, set_id);
    if(location != -1) {
        // HIT: Found it!
        self->HIT++;
        // if(self->ways == 16 && set_id == 0 && self->MISS == 0) {
        //     PySys_WriteStdout("HIT self->LOAD=%i addr=%i cl_id=%i set_id=%i\n", self->LOAD, addr, cl_id, set_id);
        // }

        if(self->replacement_policy_id == 0 || self->replacement_policy_id == 3) {
            // FIFO: nothing to do
            // RR: nothing to do
            return self->ways-1;
        } else { // if(self->replacement_policy_id == 1 || self->replacement_policy_id == 2) {
            // LRU: Reorder elements to account for access to element
            // MRU: Reorder elements to account for access to element
            if(location != 0) {
                for(int j=location; j>0; j--) {
                    self->placement[set_id*self->ways+j] =
                        self->placement[set_id*self->ways+j-1];
                }
                self->placement[set_id*self->ways].cl_id = cl_id;
                self->placement[set_id*self->ways].dirty = 0;
                self->placement[set_id*self->ways].invalid = 0;
            }
            return 0;
        } 
        // TODO if this is an exclusive cache, swap delivered cacheline with swap_cl_id (here and at end -> DO NOT RETURN)
    }

    // MISS!
    self->MISS++;
    // if(self->ways == 8 && self->sets == 64) {//self->LOAD < 200) {
    //     PySys_WriteStdout("CACHED [%i", self->placement[set_id*self->ways]);
    //     for(int i=1; i<self->ways; i++) {
    //         PySys_WriteStdout(", %i", self->placement[set_id*self->ways+i].cl_id);
    //     }
    //     PySys_WriteStdout("]\n");
    //     PySys_WriteStdout("MISS self->LOAD=%i addr=%i cl_id=%i set_id=%i\n", self->LOAD, addr, cl_id, set_id);
    // }
    
    // Load from lower cachelevel
    // TODO also check victim cache, if available
    if(self->load_from != NULL) {
        Py_INCREF(self->load_from);
        // TODO use replace_entry to inform other cache of swap (in case of exclusive caches)
        Cache__load((Cache*)self->load_from, addr); // TODO, replace_cl_id);
        Py_DECREF(self->load_from);
    } // else last-level-cache
    
    cache_entry entry;
    entry.cl_id = cl_id;
    entry.dirty = 0;
    entry.invalid = 0;
    
    // Inject new entry into own cache. This also handles replacement.
    return Cache__inject(self, &entry);
    
    // TODO if this is an exclusive cache (swap_on_load = True), swap delivered cacheline with swap_cl_id (here and at hit)
}

static void Cache__store(Cache* self, unsigned int addr) {
    self->STORE++;
    unsigned int cl_id = Cache__get_cacheline_id(self, addr);
    unsigned int set_id = Cache__get_set_id(self, cl_id);
    int location = Cache__get_location(self, cl_id, set_id);
    // PySys_WriteStdout("STORE=%i addr=%i cl_id=%i sets=%i\n", self->LOAD, addr, cl_id, self->sets);
    
    if(self->write_allocate == 1) {
        // Write-allocate policy
        
        // Make sure line is loaded into cache (this will produce HITs and MISSes):
        if(location == -1) {
            // TODO does this also make sens if store with write-allocate and MISS happens on L2?
            // or would this inject byte loads instead of CL loads into the statistic
            // TODO makes no sens if first level is write-through (all byte requests hit L2)
            for(int i=0; i<self->cl_size; i++) {
                location = Cache__load(self, cl_id*self->cl_size+i);
            }
        }
        
        // TODO handle write-allocate in lower levels without messing up the statistics
    }
    
    if(self->write_back == 1 && location != -1) {
        // Write-back policy and cache-line in cache
        
        // Mark cacheline as dirty for later write-back during eviction
        self->placement[set_id*self->ways+location].dirty = 1;
    } else {
        // Write-through policy or cache-line not in cache
        
        // Store to lower cachelevel
        // TODO use Cache__inject
        if(self->store_to != NULL) {
            Py_INCREF(self->store_to);
            Cache__store((Cache*)(self->store_to), addr);
            Py_DECREF(self->store_to);
        } // else last-level-cache
    }
}

static PyObject* Cache_load(Cache* self, PyObject *args, PyObject *kwds)
{
    unsigned int addr;
    unsigned int length = 1;
    
    static char *kwlist[] = {"addr", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I|I", kwlist, &addr, &length);
    
    // Doing this in c, tremendously increases the speed for multiple elements
    for(int i=0; i<length; i++) {
        Cache__load(self, addr+i); // TODO , 0); 
        // Swap cl_id is irrelevant here, since this is only called on first level cache
    }
    Py_RETURN_NONE;
}

static PyObject* Cache_iterload(Cache* self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    unsigned int length = 1;
    
    static char *kwlist[] = {"addrs", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "O|I", kwlist, &addrs, &length);
    
    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }

    // Iterate of elements in addrs
    PyObject *addr;
    while((addr = PyIter_Next(addrs_iter))) {
        // Each address is expanded to a certain length (default is 1)
        for(int i=0; i<length; i++) {
#if PY_MAJOR_VERSION >= 3
            Cache__load(self, PyLong_AsUnsignedLongMask(addr)+i); // TODO , 0); 
            // Swap cl_id is irrelevant here, since this is only called on first level cache
#else
            Cache__load(self, PyInt_AsUnsignedLongMask(addr)+i); // TODO , 0); 
            // Swap cl_id is irrelevant here, since this is only called on first level cache
#endif
        }
        Py_DECREF(addr);
    }
    Py_DECREF(addrs_iter);
    Py_RETURN_NONE;
}

static PyObject* Cache_store(Cache* self, PyObject *args, PyObject *kwds)
{
    unsigned int addr;
    unsigned int length = 1;
    
    static char *kwlist[] = {"addr", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I|I", kwlist, &addr, &length);
    
    // Doing this in c, tremendously increases the speed for multiple elements
    for(int i=0; i<length; i++) {
        Cache__store(self, addr+i);
    }
    Py_RETURN_NONE;
}

static PyObject* Cache_iterstore(Cache* self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    unsigned int length = 1;
    
    static char *kwlist[] = {"addrs", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "O|I", kwlist, &addrs, &length);
    
    // Get and check iterator
    PyObject *addrs_iter = PyObject_GetIter(addrs);
    if(addrs_iter == NULL) {
        PyErr_SetString(PyExc_ValueError, "addrs is not iteratable");
        return NULL;
    }
    
    // Iterate of elements in addrs
    PyObject *addr;
    while((addr = PyIter_Next(addrs_iter))) {
        // Each address is expanded to a certain length (default is 1)
        for(int i=0; i<length; i++) {
#if PY_MAJOR_VERSION >= 3
            Cache__store(self, PyLong_AsUnsignedLongMask(addr)+i);
#else
            Cache__store(self, PyLong_AsUnsignedLongMask(addr)+i);
#endif
        }
        Py_DECREF(addr);
    }
    Py_DECREF(addrs_iter);
    Py_RETURN_NONE;
}

static PyObject* Cache_loadstore(Cache* self, PyObject *args, PyObject *kwds)
{
    PyObject *addrs;
    unsigned int length = 1;
    
    static char *kwlist[] = {"addrs", "length", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "O|I", kwlist, &addrs, &length);
    
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
                // Each address is expanded to a certain length (default is 1)
                for(int i=0; i<length; i++) {
#if PY_MAJOR_VERSION >= 3
                    Cache__load(self, PyLong_AsUnsignedLongMask(addr)+i);
#else
                    Cache__load(self, PyLong_AsUnsignedLongMask(addr)+i);
#endif
                }
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
                // Each address is expanded to a certain length (default is 1)
                for(int i=0; i<length; i++) {
#if PY_MAJOR_VERSION >= 3
                    Cache__store(self, PyLong_AsUnsignedLongMask(addr)+i);
#else
                    Cache__store(self, PyLong_AsUnsignedLongMask(addr)+i);
#endif
                }
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
    if(self->write_combining == 1 && self->write_buffer.invalid == 0) {
        // We have a pending write buffered
        if(self->store_to != NULL) {
            // Found dirty line, initiate write-back:
            Py_INCREF(self->store_to);
            Cache__store((Cache*)self->store_to, self->write_buffer.cl_id*self->cl_size);
            Py_DECREF(self->store_to);
        }
        self->write_buffer.dirty = 0;
        self->write_buffer.invalid = 1;
    }
    for(int i=0; i<self->ways*self->sets; i++) {
        if(self->placement[i].invalid == 0 && self->placement[i].dirty == 1) {
            if(self->store_to != NULL) {
                // Found dirty line, initiate write-back:
                Py_INCREF(self->store_to);
                Cache__store((Cache*)self->store_to, self->placement[i].cl_id*self->cl_size);
                Py_DECREF(self->store_to);
            }
            self->placement[i].dirty = 0;
        }
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
    
    /* Sentinel */
    {NULL, NULL}
};

static PyObject* Cache_cached_get(Cache* self) {
    PyObject* cached_set = PySet_New(NULL);
    for(int i=0; i<self->sets*self->ways; i++) {
        // For each cached cacheline expand to all cached addresses:
        for(int j=0; j<self->cl_size; j++) {
            // PySys_WriteStdout("%i %i %i %i\n", self->sets, self->ways, i, self->placement[i].cl_id);
            PyObject* addr = PyLong_FromUnsignedLong(self->placement[i].cl_id*self->cl_size+j);
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
    static char *kwlist[] = {"name", "sets", "ways", "cl_size",
                             "replacement_policy_id", "write_back", "write_allocate",
                             "store_to", "load_from", "victims_to",
                             "swap_on_load", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "sIIIiiiOOOi", kwlist,
                                     &self->name, &self->sets, &self->ways, &self->cl_size,
                                     &self->replacement_policy_id,
                                     &self->write_back, &self->write_allocate,
                                     &store_to, &load_from, &victims_to,
                                     &self->swap_on_load)) {
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
            PyErr_SetString(PyExc_TypeError, "victims_to needs to be backend.Cache or None");
            return -1;
        }
        tmp = self->victims_to;
        Py_INCREF(victims_to);
        self->load_from = victims_to;
        Py_XDECREF(tmp);
    } else {
        self->victims_to = NULL;
    }
    
    // TODO validate store, load and victim paths so no null objects will be used until LLC/mem? is hit
    // should we introduce a memory object in c?

    self->placement = PyMem_New(struct cache_entry, self->sets*self->ways);
    for(unsigned int i=0; i<self->sets*self->ways; i++) {
        self->placement[i].invalid = 1;
    }
    
    // Invalidate write_buffer
    self->write_buffer.invalid = 1;

    // TODO check if ways and cl_size are of power^2
    self->set_bits = log2_uint(self->sets);
    self->cl_bits = log2_uint(self->cl_size);

    self->LOAD = 0;
    self->STORE = 0;
    self->HIT = 0;
    self->MISS = 0;
    
    //PySys_WriteStdout("CACHE sets=%i set_bits=%i ways=%i cl_size=%i cl_bits=%i\n", self->sets, self->set_bits, self->ways, self->cl_size, self->cl_bits);
    
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