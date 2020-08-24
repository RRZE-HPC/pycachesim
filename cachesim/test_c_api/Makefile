all: test

backend.o: ../backend.c ../backend.h
	gcc -DNDEBUG -O3 -g -Wall -Wstrict-prototypes -DNO_PYTHON -c ../backend.c -o backend.o

test: test.c ../backend.h backend.o
	gcc -DNDEBUG -O3 -g -Wall -Wstrict-prototypes -DNO_PYTHON -o test test.c backend.o

clean:
	rm -rf backend.o test