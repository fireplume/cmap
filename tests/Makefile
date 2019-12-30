# Memo to myself:
# $<:	name of the first prerequisite
# $@:	filename of the target of the rule
#
# _GNU_SOURCE: for gnu specifics like mremap, should avoid it though...

# librt (RealTime): 	semaphore, shared memory, ...
# libdl (DynLinker):	Dynamic linker (dlsym, dlopen,...)

first: all

.PHONY: clean


CC = gcc
LD = gcc


CCFLAGS += -O2 -I$(PROJECT_ROOT)/src/include -I$(PROJECT_ROOT)/mem -I. -fpie
LDFLAGS += -L$(OUT_DIR) -ltmap -lpthread -ldl -Wl,-rpath=.:/usr/lib:/usr/local/lib


_OBJECTS = allocSample.o maptest.o multitaskMapTest.o
OBJECTS = $(foreach o,$(_OBJECTS),$(OUT_DIR)/$(o))


# Recipes


$(OUT_DIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $< -o $@

build: $(OBJECTS)
	$(LD) $(OBJECTS) -o $(OUT_DIR)/maptest $(LDFLAGS)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

all: $(OUT_DIR) build

clean:
	rm -f $(OUT_DIR)/maptest $(OBJECTS)

# Variants

# variants.mk: defines debug and gprof variants
include $(MKFILES)/variants.mk

# Using our own malloc implementation
malloc: LDFLAGS += -L$(OUT_DIR) -lmyalloc
malloc: CCFLAGS += -DMYALLOC
malloc: clean debug
