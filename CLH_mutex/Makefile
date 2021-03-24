CFLAGS = -std=c11 -pthread

all: benchmark.out

benchmark.out: clh.o
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean

clean:
	rm *.out *.o
