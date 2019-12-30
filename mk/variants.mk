# Debug variant

debug: CCFLAGS += -O0 -g -DQADEBUG
debug: clean build

# Variant for profiling

gprof: CCFLAGS += -pg
gprof: clean debug
