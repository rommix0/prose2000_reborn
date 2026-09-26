# Builds the Prose speech library for Linux (or any POSIX system with gcc or clang):
#
#   make              build/libprose.so, build/prose_say and the samples in build/
#   make clean
#
# Speaker output loads PulseAudio or ALSA at run time, so building needs no audio packages. The tests and replay
# tools are built with CMake instead (src/CMakeLists.txt, REFERENCE §14).

CC ?= cc
CFLAGS ?= -O2
BUILD ?= build

SRC := src
LIB_DIRS := common data dsp frame input lexical paramgen prosody textrules v1 dll
LIB_SRC := $(foreach d,$(LIB_DIRS),$(wildcard $(SRC)/$(d)/*.c))
LIB_OBJ := $(patsubst $(SRC)/%.c,$(BUILD)/obj/%.o,$(LIB_SRC))
INC := $(addprefix -I$(SRC)/,$(LIB_DIRS) include)
SAMPLES := $(patsubst samples/%.c,$(BUILD)/%,$(wildcard samples/*.c))

STD := -std=c99 -Wall -Wextra
LIB := $(BUILD)/libprose.so
USE_LIB := -I$(SRC)/include -L$(BUILD) -lprose -Wl,-rpath,'$$ORIGIN'

all: $(LIB) $(BUILD)/prose_say $(SAMPLES)

$(BUILD)/obj/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(STD) $(CFLAGS) -fPIC -fvisibility=hidden -DPROSE_BUILD_DLL $(INC) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJ)
	$(CC) -shared $(LDFLAGS) -o $@ $^ -lpthread -ldl

$(BUILD)/prose_say: $(SRC)/cli/prose_say.c $(LIB)
	$(CC) $(STD) $(CFLAGS) $< -o $@ $(USE_LIB) $(LDFLAGS)

$(BUILD)/%: samples/%.c $(LIB)
	$(CC) $(STD) $(CFLAGS) $< -o $@ $(USE_LIB) -lm $(LDFLAGS)

clean:
	rm -rf $(BUILD)

.PHONY: all clean

-include $(LIB_OBJ:.o=.d)
