all: test

backend.o: backend.c backend.h
	gcc -DNDEBUG -g -O3 -Wall -Wstrict-prototypes -DNO_PYTHON -c backend.c -o backend.o

test.o: test.c backend.h
	gcc -DNDEBUG -g -O3 -Wall -Wstrict-prototypes -DNO_PYTHON -c test.c -o test.o

test: test.o backend.o
	gcc -g -o test test.o backend.o