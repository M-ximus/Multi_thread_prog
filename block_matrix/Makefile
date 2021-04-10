CXXFLAGS = -std=c++11 -pthread -MD

all: mul.out

mul.out: benchmark.cpp block_matrix.cpp block_matrix.hpp
	$(CXX) $(CXXFLAGS) -o $@ benchmark.cpp block_matrix.cpp

-include *.d

.PHONY: clean

clean:
	rm *.out *.o *.d
