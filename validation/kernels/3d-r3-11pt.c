#include <stdio.h>
#include <stdlib.h>
#include <likwid.h>

#define DTYPE double

void dummy(void *);

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s (repeat elementsz elementsy elementsx)...\n", argv[0]);
        return 1;
    }
    const int tests = (argc-1) / 4;
    int repeats[tests];
    int elementsz[tests];
    int elementsy[tests];
    int elementsx[tests];
    int maxproduct = 0;
    for(int t=0; t<tests; t++) {
        repeats[t] = atoi(argv[1+t*4]);
        elementsz[t] = atoi(argv[2+t*4]);
        elementsy[t] = atoi(argv[3+t*4]);
        elementsx[t] = atoi(argv[4+t*4]);
        if(maxproduct <elementsz[t]* elementsy[t]*elementsx[t]) {
            maxproduct = elementsz[t]*elementsy[t]*elementsx[t];
        }
    }
    printf("kernel: 3d-r3-11pt\n");
    printf("elementsize: %lu\n", sizeof(DTYPE));
    
    //SETUP
    DTYPE* a = malloc(maxproduct*sizeof(DTYPE));
    DTYPE* b = malloc(maxproduct*sizeof(DTYPE));
    for(int i=0; i<maxproduct; i++) {
        a[i] = i;
        b[i] = maxproduct-i;
    }

    likwid_markerInit();

    char cur_region_name[128];
    for(int t=0; t<tests; t++) {
        const int cur_elementsz = elementsz[t];
        const int cur_elementsy = elementsy[t];
        const int cur_elementsx = elementsx[t];
        const int cur_repeats = repeats[t];
        sprintf(cur_region_name, "3d-r3-11pt_%i_%i_%i_%i",
                cur_repeats, cur_elementsz, cur_elementsy, cur_elementsx);
        likwid_markerRegisterRegion(cur_region_name);
        printf("%s:iterations: %i\n", cur_region_name,
               (cur_elementsz-6)*(cur_elementsy-6)*(cur_elementsx-2));
        printf("%s:repetitions: %i\n", cur_region_name, cur_repeats);

        for(int warmup = 1; warmup >= 0; --warmup) {
            int repeat = 2;
            if(warmup == 0) {
                repeat = cur_repeats;
                likwid_markerStartRegion(cur_region_name);
            }

            for(; repeat > 0; --repeat) {
                for(int z=3; z<cur_elementsz-3; z++) {
                    for(int y=3; y<cur_elementsy-3; y++) {
                        for(int x=1; x<cur_elementsx-1; x++) {
                            a[z*cur_elementsy*cur_elementsx+y*cur_elementsx+x] =
                                b[(z+3)*cur_elementsy*cur_elementsx+y*cur_elementsx+x] +
                                b[(z+1)*cur_elementsy*cur_elementsx+y*cur_elementsx+x] +
                                b[z*cur_elementsy*cur_elementsx+(y-3)*cur_elementsx+x] + 
                                b[z*cur_elementsy*cur_elementsx+(y-1)*cur_elementsx+x] + 
                                b[z*cur_elementsy*cur_elementsx+y*cur_elementsx+x-1] +
                                b[z*cur_elementsy*cur_elementsx+y*cur_elementsx+x+1] +
                                b[z*cur_elementsy*cur_elementsx+(y+1)*cur_elementsx+x] +
                                b[z*cur_elementsy*cur_elementsx+(y+3)*cur_elementsx+x] +
                                b[(z-1)*cur_elementsy*cur_elementsx+y*cur_elementsx+x] +
                                b[(z-3)*cur_elementsy*cur_elementsx+y*cur_elementsx+x];
                        }
                    }
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