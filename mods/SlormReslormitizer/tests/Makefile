# Builds the optimizer natively and runs its checks: make test
CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
SOURCES = ../source/Optimizer.cpp OptimizerTest.cpp

bin/tests: $(SOURCES) ../source/Optimizer.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@

test: bin/tests
	./bin/tests

.PHONY: test
