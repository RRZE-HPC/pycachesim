#include "Python.h"
#include "backend.h"
#include <structmember.h>
#include <limits.h>

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

static void Cache_dealloc(Cache* self) {
    Py_XDECREF(self->store_to);
    Py_XDECREF(self->load_from);
    PyMem_Del(self->placement);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMemberDef Cache_members[] = {
    {"name", T_STRING, offsetof(Cache, name), 0,
     "name of cache level"},
    {"sets", T_ULONG, offsetof(Cache, sets), 0,
     "number of sets available"},
    {"ways", T_ULONG, offsetof(Cache, ways), 0,
     "number of ways available"},
    {"cl_size", T_ULONG, offsetof(Cache, cl_size), 0,
     "number of bytes in a cacheline"},
    {"cl_bits", T_ULONG, offsetof(Cache, cl_bits), 0,
     "number of bits used to identiy individual bytes in a cacheline"},
    {"subblock_size", T_ULONG, offsetof(Cache, subblock_size), 0,
     "number of bytes per subblock (must be a devisor of cl_size)"},
    {"subblock_bits", T_ULONG, offsetof(Cache, subblock_bits), 0,
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
    {"LOAD_count", T_ULONGLONG, offsetof(Cache, LOAD.count), 0,
     "number of loads performed"},
    {"LOAD_byte", T_ULONGLONG, offsetof(Cache, LOAD.byte), 0,
     "number of bytes loaded"},
    {"STORE_count", T_ULONGLONG, offsetof(Cache, STORE.count), 0,
     "number of stores performed"},
    {"STORE_byte", T_ULONGLONG, offsetof(Cache, STORE.byte), 0,
     "number of bytes stored"},
    {"HIT_count", T_ULONGLONG, offsetof(Cache, HIT.count), 0,
     "number of cache hits"},
    {"HIT_byte", T_ULONGLONG, offsetof(Cache, HIT.byte), 0,
     "number of bytes that were cache hits"},
    {"MISS_count", T_ULONGLONG, offsetof(Cache, MISS.count), 0,
     "number of misses"},
    {"MISS_byte", T_ULONGLONG, offsetof(Cache, MISS.byte), 0,
     "number of bytes missed"},
    {"EVICT_count", T_ULONGLONG, offsetof(Cache, EVICT.count), 0,
     "number of evicts"},
    {"EVICT_byte", T_ULONGLONG, offsetof(Cache, EVICT.byte), 0,
     "number of bytes evicted"},
    {"verbosity", T_INT, offsetof(Cache, verbosity), 0,
     "verbosity level of output"},
    {NULL}  /* Sentinel */
};

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
    unsigned long addr;

    static char *kwlist[] = {"addr", NULL};
    PyArg_ParseTupleAndKeywords(args, kwds, "I", kwlist, &addr);

    unsigned long cl_id = Cache__get_cacheline_id(self, addr);
    unsigned long set_id = Cache__get_set_id(self, cl_id);

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

static PyObject* Cache_count_invalid_entries(Cache* self) {
    int count = 0;
    for(int i=0; i<self->ways*self->sets; i++) {
        if(self->placement[i].invalid == 1) {
            count++;
        }
    }
    return Py_BuildValue("i", count);
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
    for(unsigned long i=0; i<self->sets*self->ways; i++) {
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