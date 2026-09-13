BIN      ?= blitz
CXX      ?= clang++
ARCH     ?= native
SRCDIR    = src
SOURCES   = $(SRCDIR)/main.cpp $(SRCDIR)/bitboard.cpp $(SRCDIR)/position.cpp \
            $(SRCDIR)/movegen.cpp $(SRCDIR)/movepick.cpp $(SRCDIR)/evaluate.cpp \
            $(SRCDIR)/search.cpp $(SRCDIR)/thread.cpp $(SRCDIR)/tt.cpp \
            $(SRCDIR)/timeman.cpp $(SRCDIR)/uci.cpp $(SRCDIR)/misc.cpp \
            $(SRCDIR)/bench.cpp $(SRCDIR)/datagen.cpp $(SRCDIR)/nnuecheck.cpp \
            $(SRCDIR)/nnue/network.cpp
OBJECTS   = $(SOURCES:.cpp=.o)
DEPS      = $(OBJECTS:.o=.d)

CXXFLAGS ?= -std=c++20 -O3 -DNDEBUG -flto=auto -fno-exceptions -fno-rtti \
            -Wall -Wextra -Wno-unused-parameter -MMD -MP -I$(SRCDIR)
LDFLAGS  ?= -flto=auto -pthread

HL ?= 128
CXXFLAGS += -DBLITZ_HL=$(HL)

ifeq ($(ARCH),native)
    ifeq ($(shell uname -m),arm64)
        CXXFLAGS += -mcpu=native
    else
        CXXFLAGS += -march=native
    endif
else ifeq ($(ARCH),x86-64-avx2)
    CXXFLAGS += -march=x86-64-v3
else ifeq ($(ARCH),x86-64)
    CXXFLAGS += -march=x86-64
else ifeq ($(ARCH),arm64)
    CXXFLAGS += -march=armv8-a
else
    $(error unknown ARCH $(ARCH), use native, x86-64-avx2, x86-64 or arm64)
endif

ifneq (,$(findstring mingw,$(shell $(CXX) -dumpmachine 2>/dev/null))$(findstring Windows_NT,$(OS)))
    ifeq ($(suffix $(BIN)),)
        BIN := $(BIN).exe
    endif
    LDFLAGS += -static -s
endif

STAMP = $(HL) $(ARCH) $(CXX)
ifneq ($(shell cat .stamp 2>/dev/null),$(STAMP))
    $(shell rm -f $(OBJECTS) $(DEPS) .stamp)
endif

all: $(BIN)

$(OBJECTS): .stamp

$(SRCDIR)/nnue/network.o: blitz.nnue

.stamp:
	@echo "$(STAMP)" > $@

$(BIN): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

debug: CXXFLAGS := $(filter-out -O3 -DNDEBUG -flto=auto,$(CXXFLAGS)) -O1 -g -fsanitize=address,undefined
debug: LDFLAGS := -pthread -fsanitize=address,undefined
debug: clean $(BIN)

perft: $(SRCDIR)/perft.cpp $(SRCDIR)/position.cpp $(SRCDIR)/movegen.cpp $(SRCDIR)/bitboard.cpp
	$(CXX) $(CXXFLAGS) $^ -o perft $(LDFLAGS)

bench: $(BIN)
	./$(BIN) bench

test: $(BIN) perft
	./tools/run_tests.sh

clean:
	rm -f $(OBJECTS) $(DEPS) $(BIN) perft .stamp

-include $(DEPS)
.PHONY: all clean debug bench test
