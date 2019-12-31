first: all

.PHONY: clean


# Common settings for all projects
include $(MKFILES)/common.mk


CCFLAGS += -Iinclude -shared -fpic
LDFLAGS += -shared -fpic -lpthread -lrt -Wl,-soname,libmyalloc.so


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
