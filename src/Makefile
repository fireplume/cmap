first: all

.PHONY: clean


CC = gcc
LD = gcc


CCFLAGS += -O2 -D_GNU_SOURCE=1 -Iinclude -shared -fpic
LDFLAGS += -shared -fpic -lpthread -lrt

OBJECTS = $(OUT_DIR)/tmap.o


# Recipes

$(OUT_DIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $< -o $@

build: $(OBJECTS)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $(OUT_DIR)/libtmap.so

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

all: $(OUT_DIR) build

clean:
	rm -f $(OUT_DIR)/libtmap.so $(OBJECTS)


# Variants

include $(MKFILES)/variants.mk

# Using our own malloc implementation
malloc: LDFLAGS += -L$(OUT_DIR) -lmyalloc
malloc: clean debug
