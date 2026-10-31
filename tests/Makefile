CXX ?= c++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Wpedantic -Werror -pthread

INCLUDE_FLAGS := -Isrc -I../src
NETWORK_SOURCES := ../src/network/discovery.cc ../src/network/protocol.cc \
	../src/network/socket.cc ../src/network/topic.cc ../src/network/transport.cc

.PHONY: all test clean

all: test

test: smoke_test m1_protocol_test m2_discovery_test m3_m4_transport_test
	./smoke_test
	./m1_protocol_test
	./m2_discovery_test
	./m3_m4_transport_test

smoke_test: smoke_test.cc
	$(CXX) $(CXXFLAGS) $< -o $@

m1_protocol_test: m1_protocol_test.cc $(NETWORK_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDE_FLAGS) $^ -o $@

m2_discovery_test: m2_discovery_test.cc $(NETWORK_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDE_FLAGS) $^ -o $@

m3_m4_transport_test: m3_m4_transport_test.cc $(NETWORK_SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDE_FLAGS) $^ -o $@

clean:
	rm -f smoke_test smoke_test.exe m1_protocol_test m1_protocol_test.exe \
		m2_discovery_test m2_discovery_test.exe
		rm -f m3_m4_transport_test m3_m4_transport_test.exe
