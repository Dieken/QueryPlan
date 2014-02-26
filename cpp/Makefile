# Option "-rdynamic" is to make symbols in the executable file visible
# to shared library opened with dlopen(), see dlopen(3) in Linux
# Programmer's Manual. This is to make sure all translation units
# use same getModuleFactoryRegistry() instance, actually the same
# static variable "registry" in getModuleFactoryRegistry().

CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra
CXXFLAGS += -rdynamic

ifdef BOOST_DIR
	CXXFLAGS += -I$(BOOST_DIR)/include
endif

all: main main-dbg

main: queryplan.hpp main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o main main.cpp

main-dbg: queryplan.hpp main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -DQP_ENABLE_TRACE=1 -DQP_ENABLE_TIMING=1 \
		-o main-dbg main.cpp

format:
	$(CXX) -E $(CXXFLAGS) main.cpp | ./format.pl --only MyModule | astyle | less

format-dbg:
	$(CXX) -E $(CXXFLAGS) -DQP_ENABLE_TRACE=1 -DQP_ENABLE_TIMING=1 \
		main.cpp | ./format.pl --only MyModule | astyle | less

format-dbg:

clean:
	-rm -f main main-dbg

.PHONY: all clean format format-dbg

