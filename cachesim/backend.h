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
    long unsigned int addr;
    unsigned int length;
} addr_range;

struct stats {
    unsigned int count;
    unsigned int byte;
    //unsigned int cl; // might be used later
};

typedef struct Cache {
    #ifndef NO_PYTHON
    PyObject_HEAD
    #endif
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
    #ifndef NO_PYTHON
    PyObject *load_from;
    PyObject *store_to;
    PyObject *victims_to;
    #else
    struct Cache *load_from;
    struct Cache *store_to;
    struct Cache *victims_to;
    #endif
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

unsigned int log2_uint(unsigned int x);

int isPowerOfTwo(unsigned int x);

// inline int Cache__get_location(Cache* self, unsigned int cl_id, unsigned int set_id);

// int Cache__inject(Cache* self, cache_entry* entry);

int Cache__load(Cache* self, addr_range range);

void Cache__store(Cache* self, addr_range range, int non_temporal);

int testLink(void);