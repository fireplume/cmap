# Common to all makefiles in this project

CC = clang
LD = clang

CCFLAGS += -Wall -O2

# Debug variant

debug: CCFLAGS += -O0 -g -DQADEBUG
debug: clean build

# Variant for profiling

gprof: CCFLAGS += -pg
gprof: clean debug
