#include "../backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char*argv[])
{
    Cache* cache = get_cacheSim_from_file("cachedef");
    addr_range range;
    
    range.addr = 2342;
    range.length = 1;
    Cache__load(cache, range);
    
    range.addr = 512;
    range.length = 8;
    Cache__store(cache, range, 0);

    Cache__load(cache, range);
    
    printStats(cache);
}