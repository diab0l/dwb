# See COPYING for copyright and license details

include ../config.mk
SETTINGS=../$(LIBDIR)/settings.html
KEYS=../$(LIBDIR)/keys.html
OUTFILES=$(SETTINGS) $(KEYS) 

all: $(OUTFILES)

$(KEYS): keys.in
	@echo gen keys.html
	@awk -f generate_keys.awk $< > $@

$(SETTINGS): settings.in
	@echo gen settings.html
	@awk -f generate_settings.awk $< > $@

clean: 
	$(RM) $(OUTFILES)

.PHONY: clean
