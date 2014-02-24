CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra

ifdef BOOST_DIR
	CXXFLAGS += -I$(BOOST_DIR)/include
	LDFLAGS += -L$(BOOST_DIR)/lib
endif

all: main main-dbg

main: queryplan.hpp main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o main main.cpp

main-dbg: queryplan.hpp main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -lboost_system-mt \
		-DBOOST_CHRONO_HEADER_ONLY -DQP_ENABLE_TRACE=1 -DQP_ENABLE_TIMING=1 \
		-o main-dbg main.cpp

format:
	$(CXX) -E $(CXXFLAGS) main.cpp | ./format.pl --only MyModule | astyle | less

format-dbg:
	$(CXX) -E $(CXXFLAGS) -DBOOST_CHRONO_HEADER_ONLY -DQP_ENABLE_TRACE=1 -DQP_ENABLE_TIMING=1 \
		main.cpp | ./format.pl --only MyModule | astyle | less

format-dbg:

clean:
	-rm -f main main-dbg

.PHONY: all clean format format-dbg

