#include "Python.h"
#include <structmember.h>

struct module_state {
    PyObject *error;
};

#if PY_MAJOR_VERSION >= 3
#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))
#endif

static PyMethodDef cachesim_methods[] = {
    {NULL, NULL}
};

typedef struct cache_entry {
    unsigned int cl_id;
    short dirty;
} cache_entry;

typedef struct Cache {
    PyObject_HEAD
    unsigned int sets;
    unsigned int set_bits;
    unsigned int ways;
    unsigned int cl_size;
    unsigned int cl_bits;
    int replacement_policy; // 0 = FIFO, 1 = LRU, 2 = MRU, 3 = RR 
                            // (state is kept in the ordering)
                            // for LFU an additional field would be required to capture state
    cache_entry *placement;
    PyObject *parent;
    unsigned int LOAD;
    unsigned int STORE;
    unsigned int HIT;
    unsigned int MISS;
} Cache;

static void Cache_dealloc(Cache* self) {
    Py_XDECREF(self->parent); // Causes a segfault, but why?
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
    {"replacement_policy", T_INT, offsetof(Cache, replacement_policy), 0,
     "replacement strategy of cachlevel"},
    // TODO support read-through, write-through and write-back without wire-allocate
    /*{"read_policy", T_INT, offsetof(Cache, read_policy), 0,
     "read policy (read-through or not) (currently UNSUPPORTED)"},
    {"write_policy", T_INT, offsetof(Cache, read_policy), 0,
     "write policy (write-through or write-back, with or without write-allocate)"},*/
    {"parent", T_OBJECT_EX, offsetof(Cache, parent), 0,
     "parent Cache object (cache level which is closer to main memory)"},
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

static void Cache__load(Cache* self, unsigned int addr) {
    self->LOAD++;
    unsigned int cl_id = Cache__get_cacheline_id(self, addr);
    unsigned int set_id = Cache__get_set_id(self, cl_id);
    //if(self->ways == 8) { //&& self->LOAD < 200) {
        // PySys_WriteStdout("LOAD=%i addr=%i cl_id=%i set_id=%i\n", self->LOAD, addr, cl_id, set_id);
    //}

    // Check if cl_id is already cached
    // TODO use sorted data structure for faster searches?
    for(int i=0; i<self->ways; i++) {
        if(self->placement[set_id*self->ways+i].cl_id == cl_id) {
            // HIT: Found it!
            self->HIT++;
            // if(self->ways == 16 && set_id == 0 && self->MISS == 0) {
            //     PySys_WriteStdout("HIT self->LOAD=%i addr=%i cl_id=%i set_id=%i\n", self->LOAD, addr, cl_id, set_id);
            // }

            if(self->replacement_policy == 0 || self->replacement_policy == 3) {
                // FIFO: nothing to do
                // RR: nothing to do
            } else if(self->replacement_policy == 1 || self->replacement_policy == 2) {
                // LRU: Reorder elements to account for access to element
                // MRU: Reorder elements to account for access to element
                if(i != 0) {
                    for(int j=i; j>0; j--) {
                        self->placement[set_id*self->ways+j] =
                            self->placement[set_id*self->ways+j-1];
                    }
                    self->placement[set_id*self->ways].cl_id = cl_id;
                    self->placement[set_id*self->ways].dirty = 0;
                }
            }
            return;
        }
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
    if(self->parent != NULL) {
        Py_INCREF(self->parent);
        Cache__load((Cache*)(self->parent), addr);
        Py_DECREF(self->parent);
    }

    // Replace other cacheline according to replacement strategy (using placement order as state)
    if(self->replacement_policy == 0 || self->replacement_policy == 1) {
        // FIFO: add to front of queue
        // LRU: add to front of queue
        if(self->ways == 8 && self->sets == 64) { // && self->LOAD < 200) {
            // PySys_WriteStdout("%i REPLACED %i with %i\n", addr, self->placement[set_id*self->ways+self->ways-1].cl_id, cl_id);
        }
        for(int i=self->ways-1; i>0; i--) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i-1];
        }
        self->placement[set_id*self->ways].cl_id = cl_id;
        self->placement[set_id*self->ways].dirty = 0;
    } else if(self->replacement_policy == 2) {
        // MRU: add to end of queue
        for(int i=0; i>self->ways-1; i++) {
            self->placement[set_id*self->ways+i] = self->placement[set_id*self->ways+i+1];
        }
        self->placement[set_id*self->ways+self->ways-1].cl_id = cl_id;
        self->placement[set_id*self->ways+self->ways-1].dirty = 0;
    } else if(self->replacement_policy == 3) {
        // RR: replace random element
        int i = rand() & (self->ways - 1);
        self->placement[set_id*self->ways+i].cl_id = cl_id;
        self->placement[set_id*self->ways+i].dirty = 0;
    }
    // if(self->ways == 8 && self->sets == 64) {//self->LOAD < 200) {
    //     PySys_WriteStdout("CACHED [%i", self->placement[set_id*self->ways].cl_id);
    //     for(int i=1; i<self->ways; i++) {
    //         PySys_WriteStdout(", %i", self->placement[set_id*self->ways+i].cl_id);
    //     }
    //     PySys_WriteStdout("]\n");
    // }
}

static void Cache__store(Cache* self, unsigned int addr) {
    self->STORE++;
    // unsigned int cl_id = Cache__get_cacheline_id(self, addr);
    // unsigned int set_id = Cache__get_set_id(self, cl_id);
    //PySys_WriteStdout("STORE=%i addr=%i cl_id=%i set_id=%i\n", self->LOAD, addr, cl_id, set_id);
    
    
    // Store to lower cachelevel
    if(self->parent != NULL) {
        Py_INCREF(self->parent);
        Cache__store((Cache*)(self->parent), addr);
        Py_DECREF(self->parent);
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
        Cache__load(self, addr+i);
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
            Cache__load(self, PyLong_AsUnsignedLong(addr)+i);
#else
            Cache__load(self, PyInt_AsUnsignedLongLongMask(addr)+i);
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
            Cache__store(self, PyLong_AsUnsignedLong(addr)+i);
#else
            Cache__store(self, PyInt_AsUnsignedLongLongMask(addr)+i);
#endif
        }
        Py_DECREF(addr);
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
        if(self->placement[set_id*self->ways+i].cl_id == cl_id) {
            Py_RETURN_TRUE;
        }
    }
    Py_RETURN_FALSE;
}

static PyMethodDef Cache_methods[] = {
    {"load", (PyCFunction)Cache_load, METH_VARARGS|METH_KEYWORDS, NULL},
    {"iterload", (PyCFunction)Cache_iterload, METH_VARARGS|METH_KEYWORDS, NULL},
    {"store", (PyCFunction)Cache_store, METH_VARARGS|METH_KEYWORDS, NULL},
    {"iterstore", (PyCFunction)Cache_iterstore, METH_VARARGS|METH_KEYWORDS, NULL},
    {"contains", (PyCFunction)Cache_contains, METH_VARARGS, NULL},
    
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
    PyObject *parent, *tmp;
    parent = NULL;
    static char *kwlist[] = {"sets", "ways", "cl_size", "replacement_policy", "parent", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "IIII|O!", kwlist,
                                     &self->sets, &self->ways, &self->cl_size,
                                     &self->replacement_policy,
                                     &CacheType, &parent)) {
        return -1;
    }
    
    // Set parent (if given)
    if(parent != NULL) {
        tmp = self->parent;
        Py_INCREF(parent);
        self->parent = parent;
        Py_XDECREF(tmp);
    } else {
        self->parent = NULL;
    }

    self->placement = PyMem_New(cache_entry, self->sets*self->ways);
    for(unsigned int i=0; i<self->sets*self->ways; i++) {
        self->placement[i].cl_id = 0;
        self->placement[i].dirty = 0;
    }

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
static int cachesim_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int cachesim_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

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