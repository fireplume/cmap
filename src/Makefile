first: all

.PHONY: clean


# Common settings for all projects
include $(MKFILES)/common.mk


CCFLAGS += -fpic
LDFLAGS += -shared -fpic -lpthread


ifneq (${NODE_BLOCK_NB_ELEMENTS},)
CCFLAGS += -DNODE_BLOCK_NB_ELEMENTS=${NODE_BLOCK_NB_ELEMENTS}
endif

ifeq (${FAST_MAP},1)
CCFLAGS += -DFAST_MAP
endif


OBJECTS = $(OUT_DIR)/tmap.o


# Recipes


$(OUT_DIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $< -o $@


build: $(OBJECTS)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $(OUT_DIR)/libtmap.so


clean:
	rm -f $(OUT_DIR)/libtmap.so $(OBJECTS)


# Using our own malloc implementation
malloc: LDFLAGS += -L$(OUT_DIR) -lmyalloc
malloc: clean build

mallocd: LDFLAGS += -L$(OUT_DIR) -lmyalloc
mallocd: clean debug
