CXX:=g++
CXX_CFLAGS:=-O2 --std=c++17 -c -fpic -MD -MP
CXX_LDFLAGS:=-shared

SRCS=$(wildcard *.cc)
INTERMEDIATES=$(patsubst %.cc,build/%.o,$(SRCS))

all: directories friendstreamer
.PHONY: all

directories: bin build
.PHONY: directories

friendstreamer: bin/friendstreamer
.PHONY: friendstreamer

clean:
	rm -rf bin/
	rm -rf build/
.PHONY: clean

bin:
	mkdir -p bin

build:
	mkdir -p build

bin/friendstreamer: $(INTERMEDIATES)
	$(CXX) $(CXX_LDFLAGS) -o $@ $^

build/%.o: %.cc
	$(CXX) $(CXX_CFLAGS) -o $@ $<

-include $(SRCS:%.cc=build/%.d)
