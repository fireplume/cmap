# By convention in this project, all artefacts are put
# under OUT_DIR environment variable
#
# You can build the project with the following:
#
#   make [clean|debug]
# or
# 	make target=[debug|grof]
# or
# 	make malloc  # malloc override
# 	make mallocd # debug

.PHONY: all clean debug malloc mallocd


NEEDED_VARS := OUT_DIR PROJECT_ROOT MKFILES
UNSET_VARS=


define check_if_set
ifeq ($($(1)),)
UNSET_VARS+=$(1)
endif
endef


$(foreach v,$(NEEDED_VARS),$(eval $(call check_if_set,$(v))))

ifneq ($(strip $(UNSET_VARS)),)
$(error You need to source env.sh before compiling, following variables are not set: $(strip $(UNSET_VARS)))
endif


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
	+$(MAKE) -C mem build
	@echo; echo ========== BUILDING src ==========;
	+$(MAKE) -C src malloc
	@echo; echo ========== BUILDING tests ==========;
	+$(MAKE) -C tests malloc

mallocd:
	@echo ========== BUILDING mem ==========;
	+$(MAKE) -C mem debug
	@echo; echo ========== BUILDING src ==========;
	+$(MAKE) -C src mallocd
	@echo; echo ========== BUILDING tests ==========;
	+$(MAKE) -C tests mallocd
