BLITZ    = blitz
SRCDIR   = $(BLITZ)/src
OUTDIR   = build
CXX      = em++

SOURCES  = $(SRCDIR)/bitboard.cpp $(SRCDIR)/position.cpp $(SRCDIR)/movegen.cpp \
           $(SRCDIR)/movepick.cpp $(SRCDIR)/evaluate.cpp $(SRCDIR)/search.cpp \
           $(SRCDIR)/thread.cpp $(SRCDIR)/tt.cpp $(SRCDIR)/timeman.cpp \
           $(SRCDIR)/uci.cpp $(SRCDIR)/misc.cpp $(SRCDIR)/bench.cpp \
           $(SRCDIR)/datagen.cpp $(SRCDIR)/nnuecheck.cpp $(SRCDIR)/nnue/network.cpp \
           wasm/main.cpp
OBJECTS  = $(patsubst %.cpp,$(OUTDIR)/%.o,$(SOURCES)) $(OUTDIR)/net.o

HL      ?= 128
HASH    ?= 16

CXXFLAGS = -std=c++20 -O3 -DNDEBUG -fno-exceptions -fno-rtti \
           -Wall -Wextra -Wno-unused-parameter -I$(SRCDIR) \
           -DBLITZ_HL=$(HL) -DBLITZ_SINGLE_THREAD -DBLITZ_DEFAULT_HASH=$(HASH)

LDFLAGS  = -O3 --no-entry \
           -sMODULARIZE=1 -sEXPORT_NAME=createBlitz \
           -sENVIRONMENT=web,worker,node \
           -sEXPORTED_FUNCTIONS=_blitz_init,_blitz_command \
           -sEXPORTED_RUNTIME_METHODS=ccall,cwrap \
           -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
           -sSTACK_SIZE=8MB -sFILESYSTEM=0

all: blitz.js

blitz.js: $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

$(OUTDIR)/net.cpp: $(BLITZ)/blitz.nnue wasm/embed_net.py
	@mkdir -p $(dir $@)
	python3 wasm/embed_net.py $< $@

$(OUTDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUTDIR)/net.o: $(OUTDIR)/net.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OUTDIR) blitz.js blitz.wasm

.PHONY: all clean
