#include <stdio.h>
#include <stdlib.h>
#include <likwid.h>

#define DTYPE double

void dummy(void *);

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s (repeat rows cols)...\n", argv[0]);
        return 1;
    }
    const int tests = (argc-1) / 3;
    int repeats[tests];
    int rows[tests];
    int cols[tests];
    int maxproduct = 0;
    int maxcols = 0;
    for(int t=0; t<tests; t++) {
        repeats[t] = atoi(argv[1+t*3]);
        rows[t] = atoi(argv[2+t*3]);
        cols[t] = atoi(argv[3+t*3]);
        if(maxproduct < rows[t]*cols[t]) {
            maxproduct = rows[t]*cols[t];
        }
        if(maxcols < cols[t]) {
            maxcols = cols[t];
        }
    }
    printf("kernel: transmatvec\n");
    printf("elementsize: %lu\n", sizeof(DTYPE));
    
    //SETUP
    DTYPE* mat = malloc(maxproduct*sizeof(DTYPE));
    DTYPE* vec = malloc(maxcols*sizeof(DTYPE));
    DTYPE* resv = malloc(maxcols*sizeof(DTYPE));
    for(int i=0; i<maxproduct; i++) {
        mat[i] = i;
    }
    for(int i=0; i<maxcols; i++) {
        vec[i] = maxcols - i;
        resv[i] = 1.0;
    }

    likwid_markerInit();

    char cur_region_name[128];
    for(int t=0; t<tests; t++) {
        const int cur_rows = rows[t];
        const int cur_cols = cols[t];
        const int cur_repeats = repeats[t];
        sprintf(cur_region_name, "transmatvec_%i_%i_%i", cur_repeats, cur_rows, cur_cols);
        likwid_markerRegisterRegion(cur_region_name);
        printf("%s:iterations: %i\n", cur_region_name, (cur_rows-2)*(cur_cols-2));
        printf("%s:repetitions: %i\n", cur_region_name, cur_repeats);

        for(int warmup = 1; warmup >= 0; --warmup) {
            int repeat = 2;
            if(warmup == 0) {
                repeat = cur_repeats;
                likwid_markerStartRegion(cur_region_name);
            }

            for(; repeat > 0; --repeat) {
                for(int r=1; r<cur_rows-1; r++) {
                    double s = 0;
                    for(int c=1; c<cur_cols-1; c++) {
                        s += mat[r+c*cur_rows]*vec[c];
                    }
                    resv[r] = s;
                }
                dummy((void*)&resv);
            }
        }
        likwid_markerStopRegion(cur_region_name);
    }
    likwid_markerClose();
    return 0;
}