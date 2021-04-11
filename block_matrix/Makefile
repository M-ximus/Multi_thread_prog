CXXFLAGS = -std=c++11 -pthread -MD

all: mul.out

mul.out: benchmark.o block_matrix.o
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

-include *.d

.PHONY: clean

clean:
	rm *.out *.o *.d
