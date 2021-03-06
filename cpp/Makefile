# Option "-rdynamic" is to make symbols in the executable file visible
# to shared library opened with dlopen(), see dlopen(3) in Linux
# Programmer's Manual. This is to make sure all translation units
# use same getModuleFactoryRegistry() instance, actually the same
# static variable "registry" in getModuleFactoryRegistry().

CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Wno-unused-parameter -DBOOST_PP_VARIADICS=1
CXXFLAGS += -rdynamic

ifdef BOOST_INCLUDE
	CXXFLAGS += -I$(BOOST_INCLUDE)
endif

all: main main-dbg

main: queryplan.hpp main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o main main.cpp

main-dbg: queryplan.hpp main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -DQP_ENABLE_TRACE=1 -DQP_ENABLE_TIMING=1 \
		-o main-dbg main.cpp

format:
	$(CXX) -E $(CXXFLAGS) main.cpp | ./format.pl --only '\w+Module' | astyle | less

format-dbg:
	$(CXX) -E $(CXXFLAGS) -DQP_ENABLE_TRACE=1 -DQP_ENABLE_TIMING=1 \
		main.cpp | ./format.pl --only '\w+Module' | astyle | less

format-dbg:

clean:
	-rm -f main main-dbg

.PHONY: all clean format format-dbg

