# Pin tool interface for pycachesim backend

Provides utilities to mark a code in C/C++, in which the memory accesses are simulated with the backend of pycachesim, to get insights on the cache behavior of that code part.

## Usage

After downloading Intel(R) Pin, the pintool for the cache simulator interface can be built. Then you can use this pintool to instrument code, that includes ```pinMarker.h```, and marks a specific code region. To define the properties of the cache, an ASCII file needs to be provided.

### Prerequisites

Download [Intel(R) Pin](https://software.intel.com/content/www/us/en/develop/articles/pin-a-binary-instrumentation-tool-downloads.html) for your operating system and unpack the directory to your desired location.

### Build

Build the pinttol with

```make obj-intel64/cachesim_itf.so PIN_ROOT=<path to pin kit>```

For the IA-32 architecture, use ```obj-ia32``` instead of ```obj-intel64```.

For changing the directory where the tool will be created, override the OBJDIR variable from the command line: 

``` make PIN_ROOT=<path to Pin kit> OBJDIR=<path to output dir> <path to output dir>/cachesim_itf.so ```

### Set markers in your code

Include ```pinMarker.h``` into your C code or for C++, include it with

```C++
extern C{
    #include pinMarker.h
}
```

Inside your code, put the start and stop function calls around the code segment, you want to analyze:

```C++
_magic_pin_start();

<your code>

_magic_pin_stop();
```

### Running your program with the cache simulation

Execute your program with Pin and the cachesim pintool:

``` <path to Pin kit>/pin -t <path to pintool build directory>/cachesim_itf.so <command line parameters> -- <your program call> ```

Possible command line parameters:

- ```-follow_calls```
  If this option is set, the tool does not just instrument the memory instructions between the markers, but all memory instructions. Still, only instructions happening when the program flow is inside the marked region are counted for the cache statistics. This is needed, when the program flow jumps outside the marked region (e.g. through a function call)

- ```-cache_file <file path>```
  specify the cache definition file. Default is ```cachedef```

### Cache Definition File

Example for an Intel(R) Xeon(R) E5-2695 v3 with activated CoD mode:

```sh
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

### Example

An Example of measuring a stream benchmark can be found in the ```tests``` directory.

You can build it inside the ```tests``` directory with

```make```

After you have built the cache simulator pintool like described above, you can run the test with

``` <path to Pin kit>/pin -t <path to pintool build directory>/cachesim_itf.so -- ./stream```

It automatically uses the provided example ```cachedef``` file for the cache properties.

The output should the give you, what happens on the cache of a Intel(R) Xeon(R) E5-2695 v3 processor during the stream benchmark:

```
following of function calls disabled

L1
LOAD: 3250001 size: 64000064 B
STORE: 1000000 size: 16000000 B
HIT: 2249997 size: 35999952 B
MISS: 1000004 size: 28000112 B
EVICT: 249873 size: 15991872 B

L2
LOAD: 1000004 size: 64000256 B
STORE: 249873 size: 15991872 B
HIT: 0 size: 0 B
MISS: 1000004 size: 64000256 B
EVICT: 248977 size: 15934528 B

L3
LOAD: 1000004 size: 64000256 B
STORE: 248977 size: 15934528 B
HIT: 0 size: 0 B
MISS: 1000004 size: 64000256 B
EVICT: 213137 size: 13640768 B
```
