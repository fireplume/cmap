first: all

.PHONY: clean


CC = gcc
LD = gcc


CCFLAGS += -O2 -D_GNU_SOURCE=1 -Iinclude -shared -fpic
LDFLAGS += -shared -fpic -lpthread -lrt


OBJECTS = $(OUT_DIR)/myalloc.o


# Recipes

$(OUT_DIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $< -o $@

build: $(OBJECTS)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $(OUT_DIR)/libmyalloc.so

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

all: $(OUT_DIR) build

clean:
	rm -f $(OUT_DIR)/libmyalloc.so $(OBJECTS)


# Variants

# variants.mk: defines debug and malloc variants
include $(MKFILES)/variants.mk
