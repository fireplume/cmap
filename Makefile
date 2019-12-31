# By convention in this project, all artefacts are put
# under OUT_DIR environment variable or $(PWD)/out
# if undefined.

.PHONY: malloc

ifndef OUT_DIR
$(error You need to source env.sh before compiling)
endif

# These projects are not supported from the root folder:
# arrays, demo, strmaps

SUBPROJECTS = src mem tests

all:
	@+$(foreach p,$(SUBPROJECTS),echo; echo ========== BUILDING ${p} ==========; $(MAKE) -C $(p) $(target);)
	$(ifeq $(target),clean,$(shell rm -f $(OUT_DIR)/*;))

clean:
	+$(foreach p,$(SUBPROJECTS),$(MAKE) -C $(p) clean;)
	rm -f $(OUT_DIR)/*

debug:
	@+$(foreach p,$(SUBPROJECTS),echo; echo ========== BUILDING ${p} ==========; $(MAKE) -C $(p) debug;)


# More complex sub project's specific cases


malloc:
	@echo ========== BUILDING mem ==========;
	+$(MAKE) -C mem debug
	@echo; echo ========== BUILDING src ==========;
	+$(MAKE) -C src malloc
	@echo; echo ========== BUILDING tests ==========;
	+$(MAKE) -C tests malloc
