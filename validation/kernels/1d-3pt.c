#include <stdio.h>
#include <stdlib.h>
#include <likwid.h>

#define DTYPE double

void dummy(void *);

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s (repeat elementsx)...\n", argv[0]);
        return 1;
    }
    const int tests = (argc-1) / 2;
    int repeats[tests];
    int elementsx[tests];
    int maxelementsy = 0;
    int maxelementsx = 0;
    for(int t=0; t<tests; t++) {
        repeats[t] = atoi(argv[1+t*2]);
        elementsx[t] = atoi(argv[2+t*2]);
        if(maxelementsx < elementsx[t]) {
            maxelementsx = elementsx[t];
        }
    }
    printf("kernel: 1d-3pt\n");
    printf("elementsize: %lu\n", sizeof(DTYPE));
    
    //SETUP
    DTYPE* a = malloc(maxelementsx*sizeof(DTYPE));
    DTYPE* b = malloc(maxelementsx*sizeof(DTYPE));
    for(int i=0; i<maxelementsx; i++) {
        a[i] = i;
        b[i] = maxelementsx-i;
    }

    likwid_markerInit();

    char cur_region_name[128];
    for(int t=0; t<tests; t++) {
        const int cur_elementsx = elementsx[t];
        const int cur_repeats = repeats[t];
        sprintf(cur_region_name, "1d-3pt_%i_%i", cur_repeats, cur_elementsx);
        likwid_markerRegisterRegion(cur_region_name);
        printf("%s:iterations: %i\n", cur_region_name, cur_elementsx-2);
        printf("%s:repetitions: %i\n", cur_region_name, cur_repeats);

        for(int warmup = 1; warmup >= 0; --warmup) {
            int repeat = 2;
            if(warmup == 0) {
                repeat = cur_repeats;
                likwid_markerStartRegion(cur_region_name);
            }

            for(; repeat > 0; --repeat) {
                for(int x=1; x<cur_elementsx-1; x++) {
                    a[x] = b[x-1] + b[x] + b[x+1];
                }
                double* c = a;
                a = b;
                b = c;
                dummy((void*)&a);
            }
        }
        likwid_markerStopRegion(cur_region_name);
    }
    likwid_markerClose();
    free(a);
    return 0;
}