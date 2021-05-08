CXXFLAGS = -std=c++11 -pthread -MD -Wall -Wextra -Werror
SPEEDTEST = -DSPEED

all: corr_test

speed_test: CXXFLAGS += $(SPEEDTEST)
speed_test: benchmark.out

corr_test: benchmark.out

benchmark.out: test.o spinlocks.o
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.c
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include *.d

.PHONY: clean

clean:
	rm *.d *.out *.o
