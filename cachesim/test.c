// #include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char*argv[])
{
    FILE* stream = fopen("cachedef", "r");
    // if (stream == NULL) exit(EXIT_FAILURE);

    char line[1024];
    char* ret = fgets(line, 1024, stream);
    return 1;
}