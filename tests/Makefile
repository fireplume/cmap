first: all

.PHONY: clean


# Common settings for all projects
include $(MKFILES)/common.mk

 
CCFLAGS += -Iinclude -fpie
LDFLAGS += -L$(OUT_DIR) -ltmap -lpthread -Wl,-rpath=.:/usr/lib:/usr/local/lib


# Test targets

TARGETS = maptest ll_test


# Objects

OBJ_maptest = allocSample.o maptest.o multitaskMapTest.o
OBJ_ll_test = ll_test.o

# Remap objects into out directory 
$(foreach t,$(TARGETS),$(eval OBJECTS_$(t)=$(foreach o,$(OBJ_$(t)),$(OUT_DIR)/$(o))))


# Recipes


# This doesn't work (misses the malloc(d) tweaks).
# define SET_RECIPE
# $(1): $(OBJECTS_$(1))
# 	$(LD) $(OBJECTS_$(1)) -o $(OUT_DIR)/$(1) $(LDFLAGS)
# endef
# $(foreach t,$(TARGETS),$(eval $(call SET_RECIPE,$(t))))
# But the following works, go figure.
maptest: $(OBJECTS_maptest)
	$(LD) $(OBJECTS_maptest) -o $(OUT_DIR)/maptest $(LDFLAGS)


ll_test: $(OBJECTS_ll_test)
	$(LD) $(OBJECTS_ll_test) -o $(OUT_DIR)/ll_test $(LDFLAGS)


$(OUT_DIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $< -o $@


build: $(TARGETS)


OBJECTS = $(foreach t,$(TARGETS),$(OBJECTS_$(t)))


clean:
	rm -f $(foreach t,$(TARGETS),$(OUT_DIR)/$(t)) $(OBJECTS)


# Variants

# Using our own malloc implementation
malloc: LDFLAGS += -lmyalloc
malloc: clean build

mallocd: LDFLAGS += -lmyalloc
mallocd: clean debug


