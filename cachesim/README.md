# C API for pycachesim backend

Provides access to stripped down functionalities of pycachesim using C.

## Usage

After including the backend header, a function to create a cache object, functions to issue load and store instructions to the cache and a function to print the stats of the cache to stdout will be available. The backend then has to be compiled and linked. For the creation of the cache object, an ASCII file containing the properties of the cache ahas to be provided.

### Cache Definition File

Example for an Intel Intel(R) Xeon(R) CPU E5-2695 v3 with activated CoD mode:

```
3
# this line will be ignored
name=L1,sets=64,ways=8,cl_size=64,replacement_policy_id=1,write_back=1,write_allocate=1,subblock_size=64,load_from=L2,store_to=L2
name=L2,sets=512,ways=8,cl_size=64,replacement_policy_id=1,write_back=1,write_allocate=1,load_from=L3,store_to=L3
name=L3,sets=9216,ways=16,cl_size=64,replacement_policy_id=1,write_back=1,write_allocate=1
```

- the first line must contain the number of ache levels
- each line, that is not empty and that does not start with '#' will be considered a cache level
- each key and value are separated by '='
- key-value pairs are separated by ',' without whitespaces
- load_from, store_to, victims_to have to be values equal to the name of one of the other cache levels
- the name of a cache level must be unique
- possible keys are: 
  | Key | Value Datatype |
  |-----|----------------|
  |name|string|
  |sets|uint|
  |ways|uint|
  |cl_size|uint|
  |cl_bits|uint|
  |subblock_size|uint|
  |subblock_bits|uint|
  |replacement_policy_id|0 = FIFO, 1 = LRU, 2 = MRU, 3 = RR|
  |write_back|bool|
  |write_allocate|bool|
  |write_combining|bool|
  |swap_on_load|bool|
  |load_from|string|
  |store_to|string|
  |victims_to|string|

### Creating and Using the Cache Object

A cache object can be created with

```C
Cache* cache = get_cacheSim_from_file("/PATH/TO/CACHE/DEFINITION/FILE");
```

To issue load and stores to the cache, an address range struct is needed:

```C
addr_range range;
range.addr = 2342; // long unsigned int for the memory address
range.length = 1; // length of the range in bytes
```

Then load and store instructions can be issued to the cache object:

```C
Cache__load(cache, range);
Cache__store(cache, range, 0); // third argument: bool, if store is non temporal
```

When finished, the stats for hits and misses can be printed to stdout:

```C
printStats(cache);
```

### Build

The object file for the backend can be compiled with any C compiler and has to define the variable ```NO_PYTHON```, to exclude python interface exclusive code:

```sh
gcc -DNO_PYTHON -c backend.c -o backend.o
```

When compiling the file, where the backend header has been included, ```NO_PYTHON``` also has to be defined. Then, the backend object file can be linked in the standard way:

```sh

gcc -DNO_PYTHON -o example example.c backend.o
```

### Example

An example can be found in the ```test_c_api```

Build the example inside this directory with

```make```

Then run it with

```./test```

The output should look like the following:

```
L1:
LOAD: 3   size: 73B
STORE: 1   size: 8B
HIT: 1   size: 8B
MISS: 2   size: 65B
EVICT: 0   size: 0B
L2:
LOAD: 2   size: 128B
STORE: 0   size: 0B
HIT: 0   size: 0B
MISS: 2   size: 128B
EVICT: 0   size: 0B
L3:
LOAD: 2   size: 128B
STORE: 0   size: 0B
HIT: 0   size: 0B
MISS: 2   size: 128B
EVICT: 0   size: 0B
```

Note: The store issued in the code is not seen in the L2 and L3, as it has not been evicted from the L1 yet.