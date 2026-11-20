# cdc-pattern-compress: builds bin/dedup (the chunker) and bin/main (the coder
# benchmark driver). Objects and dependency files go in build/.
#
#   make            build both binaries
#   make clean      remove bin/ and build/

CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c99 -Wall -Wextra
CPPFLAGS += -Isrc -MMD -MP
LDLIBS  += -lm

BIN   := bin
BUILD := build

COMMON_SRCS := src/common/bitio.c src/common/buf.c src/common/io.c \
               src/common/model.c src/common/arithcoder.c src/common/seq.c
ENC_SRCS    := src/encoders/mcd.c src/encoders/amc_t.c src/encoders/amc_g.c
MAIN_SRCS   := src/main.c $(COMMON_SRCS) $(ENC_SRCS)
DEDUP_SRCS  := src/chunker/dedup.c

MAIN_OBJS  := $(MAIN_SRCS:src/%.c=$(BUILD)/%.o)
DEDUP_OBJS := $(DEDUP_SRCS:src/%.c=$(BUILD)/%.o)

.PHONY: all clean
all: $(BIN)/main $(BIN)/dedup

$(BIN)/main: $(MAIN_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/dedup: $(DEDUP_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILD)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BIN) $(BUILD)

-include $(MAIN_OBJS:.o=.d) $(DEDUP_OBJS:.o=.d)
