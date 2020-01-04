first: all

.PHONY: clean


# Common settings for all projects
include $(MKFILES)/common.mk


CCFLAGS += -fpic
LDFLAGS += -shared -fpic -lpthread -lrt -Wl,-soname,libmyalloc.so

CCFLAGS_test += -fpie -g -I$(PROJECT_ROOT)/include
LDFLAGS_test += -L$(PROJECT_ROOT)/out -lmyalloc


ifeq (${MEM_DEBUG},1)
CCFLAGS += -DMEM_DEBUG
endif


ifeq (${FAST_ALLOC},1)
CCFLAGS += -DFAST_ALLOC
endif


OBJECTS = $(OUT_DIR)/myalloc.o
OBJECTS_test = $(OUT_DIR)/malloc-test.o


# Recipes


$(OUT_DIR)/myalloc.o: myalloc.c
	$(CC) -c $(CCFLAGS) $< -o $@

$(OUT_DIR)/malloc-test.o: malloc-test.c
	$(CC) -c $(CCFLAGS_test) $< -o $@


build: clean $(OBJECTS) $(OBJECTS_test)
	$(LD) $(OBJECTS) $(LDFLAGS) -o $(OUT_DIR)/libmyalloc.so
	$(LD) $(OBJECTS_test) $(LDFLAGS_test) -o $(OUT_DIR)/malloc-test


clean:
	rm -f $(OUT_DIR)/libmyalloc.so $(OUT_DIR)/malloc-test.o $(OBJECTS) $(OBJECTS_test)

