ifeq ($(shell $(BASEDIR)/$(TOOLDIR)/check_header.sh execinfo.h $(CC)), 1)
CFLAGS += -DHAS_EXECINFO
endif
