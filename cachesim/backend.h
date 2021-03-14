
// Array of bits as found in comp.lang.c FAQ Question 20.8: http://c-faq.com/misc/bitsets.html
#define BITMASK(b) (1 << ((b) % CHAR_BIT))
#define BITSLOT(b) ((b) / CHAR_BIT)
#define BITSET(a, b) ((a)[BITSLOT(b)] |= BITMASK(b))
#define BITCLEAR(a, b) ((a)[BITSLOT(b)] &= ~BITMASK(b))
#define BITTEST(a, b) ((a)[BITSLOT(b)] & BITMASK(b))
#define BITNSLOTS(nb) ((nb + CHAR_BIT - 1) / CHAR_BIT)

typedef struct cache_entry {
    long cl_id;

    unsigned int dirty : 1; // if 0, content is in sync with main memory. if 1, it is not.
                            // used for write-back
    unsigned int invalid : 1; // denotes an entry which does not contain a valid cacheline.
                              // it is empty.
} cache_entry;

typedef struct addr_range {
    // Address range used to communicate consecutive accesses
    // last addr of range is addr+length-1
    long addr;
    long length;
} addr_range;

struct stats {
    long long count;
    long long byte;
    //long cl; // might be used later
};

typedef struct Cache {
#ifndef NO_PYTHON
    PyObject_HEAD
#endif
    const char *name;
    long sets;
    long ways;
    long cl_size;
    long cl_bits;
    long subblock_size;
    long subblock_bits;
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

int Cache__load(Cache* self, addr_range range);

void Cache__store(Cache* self, addr_range range, int non_temporal);

//!might break for complicated cache structures
void dealloc_cacheSim(Cache*);

Cache* get_cacheSim_from_file(const char* file);
// Cache* get_cacheSim_from_file(char** lines, int size);

#ifndef USE_PIN
void printStats(Cache* cache);
#endif
