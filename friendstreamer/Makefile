CXX:=g++
CXX_CFLAGS:=-O2 --std=c++17 -c -fPIC -MD -MP
CXX_CFLAGS_PROTO:=-O2 --std=c++17 -c -fPIC
CXX_LDFLAGS:=-shared

SRCS=$(wildcard *.cc)
PROTO_SRCS=$(wildcard *.proto)
PROTO_GENERATED=$(patsubst %.proto,proto-generated/%.pb.cc,$(PROTO_SRCS))

INTERMEDIATES=$(patsubst %.cc,build/%.o,$(SRCS))
PROTO_INTERMEDIATES=$(patsubst proto-generated/%.pb.cc,proto-build/%.o,$(PROTO_GENERATED))

all: directories generate-proto friendstreamer
.PHONY: all

generate-proto: directories $(PROTO_GENERATED)
.PHONY: generate-proto

directories: bin build proto-generated proto-build
.PHONY: directories

friendstreamer: bin/friendstreamer
.PHONY: friendstreamer

clean:
	rm -rf bin/
	rm -rf build/
	rm -rf proto-generated/
	rm -rf proto-build/
.PHONY: clean

bin:
	mkdir -p bin

build:
	mkdir -p build

proto-generated:
	mkdir -p proto-generated

proto-build:
	mkdir -p proto-build

bin/friendstreamer: $(INTERMEDIATES) $(PROTO_INTERMEDIATES)
	$(CXX) $(CXX_LDFLAGS) -o $@ $^

build/%.o: %.cc
	$(CXX) $(CXX_CFLAGS) -o $@ $<

proto-build/%.o: proto-generated/%.pb.cc
	$(CXX) $(CXX_CFLAGS_PROTO) -o $@ $<

proto-generated/%.pb.cc: %.proto
	protoc --cpp_out=proto-generated/ $<

-include $(SRCS:%.cc=build/%.d)
