CC := gcc

all: stream

stream: test_stream.c ../pinMarker.h
	$(CC) -O3 -o stream test_stream.c

clean:
	rm -rf stream