first: all

.PHONY: clean


# Common settings for all projects
include $(MKFILES)/common.mk


CCFLAGS += -Iinclude -shared -fpic
LDFLAGS += -shared -fpic -lpthread -lrt -Wl,-soname,libmyalloc.so


ifeq (${FAST_ALLOC},1)
CCFLAGS += -DFAST_ALLOC
endif


OBJECTS = $(OUT_DIR)/myalloc.o


# Recipes

$(OUT_DIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $< -o $@

build: clean $(OBJECTS)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $(OUT_DIR)/libmyalloc.so

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

all: $(OUT_DIR) build

clean:
	rm -f $(OUT_DIR)/libmyalloc.so $(OBJECTS)

