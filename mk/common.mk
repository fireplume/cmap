# Common to all makefiles in this project

CC = clang
LD = clang

CCFLAGS += -Wall -O2 -I$(PROJECT_ROOT)/include

# Debug variant

debug: CCFLAGS += -O0 -g -DQADEBUG
debug: clean build

# Variant for profiling

gprof: CCFLAGS += -pg
gprof: clean debug


ifndef ${OUT_DIR}
OUT_DIR = $(PROJECT_ROOT)/out
endif


$(OUT_DIR):
	mkdir -p $(OUT_DIR)


# "all" should be the first target in each makefile
all: $(OUT_DIR) build
