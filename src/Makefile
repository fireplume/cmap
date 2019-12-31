first: all

.PHONY: clean


# Common settings for all projects
include $(MKFILES)/common.mk


CCFLAGS += -Iinclude -shared -fpic
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


# Using our own malloc implementation
malloc: LDFLAGS += -L$(OUT_DIR) -lmyalloc
malloc: clean debug
